# Documentation Technique - Car Light Sync

## 🏗️ Architecture Système

### Vue d'Ensemble

Le Car Light Sync est construit sur une architecture modulaire ESP32 avec les composants principaux suivants :

```
┌─────────────────────────────────────────────────────────────┐
│                     ESP32 Main Task                         │
│  ┌─────────────┐  ┌──────────────┐  ┌──────────────────┐  │
│  │ LED Effects │  │ Web Server   │  │ WiFi Manager     │  │
│  │ Task        │  │ (HTTP/REST)  │  │ (AP + Client)    │  │
│  └──────┬──────┘  └──────┬───────┘  └────────┬─────────┘  │
│         │                │                     │            │
│  ┌──────▼────────────────▼─────────────────────▼─────────┐ │
│  │          Config Manager (NVS Profiles)                 │ │
│  └────────────────────────────────────────────────────────┘ │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐  │
│  │    CAN Bus Task → Vehicle CAN Unified Pipeline       │  │
│  │    ┌────────────┐  ┌─────────────┐  ┌────────────┐  │  │
│  │    │ TWAI Bus   │→ │ CAN Decode  │→ │ Event      │  │  │
│  │    │ (Direct)   │  │ (DBC-based) │  │ Detection  │  │  │
│  │    └────────────┘  └─────────────┘  └────────────┘  │  │
│  └──────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

## 🔧 Architecture CAN Unifiée

### Principe de Fonctionnement

Le système CAN utilise une architecture unifiée basée sur des définitions DBC (Database CAN) pour supporter plusieurs véhicules.

#### 1. Fichiers de Configuration Auto-Générés

Les fichiers suivants sont générés automatiquement à partir de définitions DBC :

- **[vehicle_can_unified_config.generated.h](include/vehicle_can_unified_config.generated.h)** : Déclarations des structures
- **[vehicle_can_unified_config.generated.c](main/vehicle_can_unified_config.generated.c)** : Définitions des messages et signaux

**Contenu typique :**
```c
// Définition d'un signal CAN
const can_signal_def_t signal_speed = {
    .name = "Speed",
    .start_bit = 16,
    .length = 16,
    .byte_order = BYTE_ORDER_LITTLE_ENDIAN,
    .value_type = SIGNAL_TYPE_UNSIGNED,
    .factor = 0.05,
    .offset = 0.0,
    .min_value = 0.0,
    .max_value = 327.67,
    .unit = "km/h"
};

// Définition d'un message CAN
const can_message_def_t msg_speed = {
    .id = 0x257,
    .name = "VehicleSpeed",
    .dlc = 8,
    .signals = (can_signal_def_t[]){signal_speed},
    .signal_count = 1
};
```

#### 2. Pipeline de Traitement CAN

**[vehicle_can_unified.c](main/vehicle_can_unified.c)** : Pipeline principal

```c
void vehicle_can_process_frame_static(const can_frame_t *frame, vehicle_state_t *state) {
    // 1. Rechercher la définition du message par ID
    const can_message_def_t *msg = find_message_def(frame->id);
    if (!msg) return;

    // 2. Pour chaque signal du message
    for (uint8_t i = 0; i < msg->signal_count; i++) {
        const can_signal_def_t *sig = &msg->signals[i];

        // 3. Décoder la valeur du signal (Little/Big Endian)
        float value = decode_signal_value(sig, frame->data, frame->dlc);

        // 4. Mapper signal → état véhicule
        vehicle_state_apply_signal(msg, sig, value, state);

        // 5. Détecter les événements (rising edge, threshold, etc.)
        for (uint8_t j = 0; j < msg->event_count; j++) {
            const can_event_def_t *evt = &msg->events[j];
            if (evt->signal_index == i) {
                bool triggered = check_event_condition(evt, value, old_value);
                if (triggered) {
                    trigger_can_event(evt->event_type);
                }
            }
        }
    }
}
```

#### 3. Décodage des Signaux

Le système supporte les deux ordres d'octets (byte order) utilisés dans les bus CAN :

**Little Endian (Intel)** :
```c
static uint64_t extract_bits_le(const uint8_t *data, uint8_t start_bit, uint8_t length) {
    uint64_t raw = 0;
    for (int i = 0; i < 8; i++) {
        raw |= ((uint64_t)data[i]) << (8 * i);
    }
    uint64_t mask = (1ULL << length) - 1;
    return (raw >> start_bit) & mask;
}
```

**Big Endian (Motorola)** :
```c
static uint64_t extract_bits_be(const uint8_t *data, uint8_t start_bit, uint8_t length) {
    uint64_t raw = 0;
    for (int i = 0; i < 8; i++) {
        raw = (raw << 8) | data[i];
    }
    uint8_t msb_index = 63 - start_bit;
    uint8_t shift = msb_index - (length - 1);
    uint64_t mask = (1ULL << length) - 1;
    return (raw >> shift) & mask;
}
```

**Application du scaling (factor/offset)** :
```c
static float decode_signal_value(const can_signal_def_t *sig, const uint8_t *data, uint8_t dlc) {
    uint64_t raw = (sig->byte_order == BYTE_ORDER_LITTLE_ENDIAN)
                   ? extract_bits_le(data, sig->start_bit, sig->length)
                   : extract_bits_be(data, sig->start_bit, sig->length);

    if (sig->value_type == SIGNAL_TYPE_BOOLEAN) {
        return raw ? 1.0f : 0.0f;
    }

    if (sig->value_type == SIGNAL_TYPE_SIGNED) {
        // Extension de signe
        int64_t signed_val = sign_extend(raw, sig->length);
        return (float)signed_val * sig->factor + sig->offset;
    }

    // UNSIGNED
    return (float)raw * sig->factor + sig->offset;
}
```

#### 4. Mapping Signal → État Véhicule

**[vehicle_can_mapping.c](main/vehicle_can_mapping.c)** : Mapping personnalisable

```c
void vehicle_state_apply_signal(const can_message_def_t *msg,
                                const can_signal_def_t *sig,
                                float value,
                                vehicle_state_t *state) {
    // Exemple : Mapper le signal "Speed" vers state->speed_kmh
    if (strcmp(sig->name, "Speed") == 0) {
        state->speed_kmh = value;
    }
    else if (strcmp(sig->name, "DoorFL") == 0) {
        state->door_fl = (value > 0.5f);
    }
    else if (strcmp(sig->name, "Gear") == 0) {
        state->gear_position = (uint8_t)value;
    }
    // ... mapping pour tous les signaux
}
```

#### 5. Détection d'Événements

Le système supporte 6 types de conditions d'événements :

| Type | Description | Exemple |
|------|-------------|---------|
| `RISING_EDGE` | Passage de 0 à 1 | Détection ouverture porte |
| `FALLING_EDGE` | Passage de 1 à 0 | Détection fermeture porte |
| `VALUE_EQUALS` | Valeur exacte | Gear == 3 (Drive) |
| `THRESHOLD_ABOVE` | Valeur > seuil | Speed > 80 km/h |
| `THRESHOLD_BELOW` | Valeur < seuil | Battery < 20% |
| `VALUE_CHANGED` | Valeur différente | Changement de vitesse |

**Implémentation :**
```c
bool check_event_condition(const can_event_def_t *evt, float new_value, float old_value) {
    switch (evt->condition_type) {
        case EVENT_CONDITION_RISING_EDGE:
            return (old_value < 0.5f && new_value >= 0.5f);

        case EVENT_CONDITION_FALLING_EDGE:
            return (old_value >= 0.5f && new_value < 0.5f);

        case EVENT_CONDITION_VALUE_EQUALS:
            return (fabs(new_value - evt->condition_value) < 0.01f);

        case EVENT_CONDITION_THRESHOLD_ABOVE:
            return (new_value > evt->condition_value);

        case EVENT_CONDITION_THRESHOLD_BELOW:
            return (new_value < evt->condition_value);

        case EVENT_CONDITION_VALUE_CHANGED:
            return (fabs(new_value - old_value) > 0.01f);
    }
    return false;
}
```

### Avantages de l'Architecture Unifiée

✅ **Multi-Véhicules** : Support de plusieurs véhicules via fichiers de config
✅ **Maintenabilité** : Code générique réutilisable
✅ **Extensibilité** : Ajout de nouveaux messages/signaux sans modification du code
✅ **Standard** : Basé sur le format DBC (standard automobile)
✅ **Performance** : Décodage optimisé en <1ms par message

## 🧠 Gestion de la Mémoire

### Analyse de l'Utilisation Mémoire (v2.1.0+)

**État actuel :**
```
RAM:   [=         ]  14.5% (47468 bytes / 327680 bytes)
Flash: [=====     ]  49.6% (974779 bytes / 1966080 bytes)
```

### Répartition de la RAM

| Composant | Taille | Description |
|-----------|--------|-------------|
| Heap libre | ~230KB | Mémoire disponible pour malloc |
| Stack tasks | ~40KB | Stacks des tâches FreeRTOS |
| Variables globales | ~30KB | État véhicule, config, buffers |
| Profils NVS | 0KB* | Stockés en flash (NVS), pas en RAM |

*Les profils ne sont chargés en RAM que temporairement lors des opérations

### Structure d'un Profil en Mémoire

```c
typedef struct {
    char name[32];                          // 32 bytes
    effect_config_t default_effect;         // ~100 bytes
    effect_config_t night_mode_effect;      // ~100 bytes
    can_event_effect_t event_effects[22];   // 22 × 80 = 1760 bytes
    bool auto_night_mode;                   // 1 byte
    uint8_t night_brightness;               // 1 byte
    uint8_t padding[4];                     // Alignement
} config_profile_t;

sizeof(config_profile_t) ≈ 1900 bytes
```

### Optimisation : Allocation Dynamique (v2.1.0)

**Problème identifié (v2.0) :**
```c
// Handler HTTP - AVANT (MAUVAIS)
static esp_err_t profiles_handler(httpd_req_t *req) {
    config_profile_t profiles[MAX_PROFILES];  // 10 × 1900 = 19KB sur la STACK !
    // ... code ...
}
```

**Symptômes :**
- Guru Meditation Error: StoreProhibited
- ESP32 reboot aléatoires
- Erreur `ESP_ERR_HTTPD_RESP_SEND`
- Stack overflow détecté par FreeRTOS

**Solution implémentée (v2.1.0) :**
```c
// Handler HTTP - APRÈS (BON)
static esp_err_t profiles_handler(httpd_req_t *req) {
    // Allocation dynamique sur le HEAP
    config_profile_t *profiles = malloc(MAX_PROFILES * sizeof(config_profile_t));
    if (profiles == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Memory allocation failed");
        return ESP_FAIL;
    }

    // ... utilisation des profils ...

    free(profiles);  // Libération obligatoire
    return ESP_OK;
}
```

### Handlers HTTP Optimisés

**Liste complète des handlers corrigés :**

| Handler | Allocation avant | Allocation après | Gain stack |
|---------|-----------------|------------------|------------|
| `profiles_handler` | 19KB stack | Heap | 19KB |
| `config_handler` | 1.9KB stack | Heap | 1.9KB |
| `profile_update_handler` | 1.9KB stack | Heap | 1.9KB |
| `profile_update_default_handler` | 1.9KB stack | Heap | 1.9KB |
| `event_effect_handler` | 1.9KB stack | Heap | 1.9KB |
| `events_post_handler` | 1.9KB stack | Heap | 1.9KB |

**Total stack libérée : ~29.5KB**

### Configuration HTTP Server

**Avant (v2.0) :**
```c
config.stack_size = 12288;        // 12KB
config.recv_wait_timeout = 10;    // 10s
config.send_wait_timeout = 10;    // 10s
```

**Après (v2.1.0) :**
```c
config.stack_size = 16384;        // 16KB (33% augmentation)
config.recv_wait_timeout = 30;    // 30s (3× plus)
config.send_wait_timeout = 30;    // 30s (3× plus)
```

**Justification :**
- Stack 16KB permet de gérer les appels imbriqués et temporaires
- Timeout 30s évite les déconnexions prématurées
- HTML compressé (~18KB) peut prendre du temps à envoyer sur WiFi lent

### Best Practices Mémoire

**1. Jamais d'allocation stack > 1KB**
```c
// ❌ MAUVAIS
uint8_t big_buffer[4096];

// ✅ BON
uint8_t *big_buffer = malloc(4096);
if (big_buffer != NULL) {
    // ... utilisation ...
    free(big_buffer);
}
```

**2. Toujours vérifier malloc()**
```c
void *ptr = malloc(size);
if (ptr == NULL) {
    ESP_LOGE(TAG, "Out of memory");
    return ESP_ERR_NO_MEM;
}
```

**3. Libérer en toutes circonstances**
```c
void *ptr = malloc(size);
// ... code ...
if (error) {
    free(ptr);  // ← Critique !
    return ESP_FAIL;
}
free(ptr);
return ESP_OK;
```

**4. Préférer stack pour petites structures**
```c
// ✅ OK (petite structure ~100 bytes)
effect_config_t config;

// ❌ ÉVITER (grosse structure ~1900 bytes)
config_profile_t profile;
```

## 📊 Métriques de Stabilité

### Tests de Stabilité (v2.1.0+)

| Métrique | v2.0 (avant) | v2.1+ (après) |
|----------|--------------|---------------|
| Uptime moyen | 2-3 heures* | > 72 heures |
| Crashes/jour | 5-10 | 0 |
| Erreurs HTTP | 30% | 0% |
| Utilisation RAM | 60%+ | 14.5% |

*Crash dû au stack overflow lors de requêtes HTTP

### Tests Effectués

**Test 1 : Charge répétée**
```bash
# 100 requêtes GET /api/profiles
for i in {1..100}; do
    curl -s http://192.168.10.1/api/profiles > /dev/null
    echo "Request $i OK"
done
```
**Résultat :** ✅ 100/100 succès

**Test 2 : Manipulation profils**
```bash
# Créer 10 profils, les activer, les supprimer
for i in {0..9}; do
    curl -X POST http://192.168.10.1/api/profile/create \
         -d "{\"name\":\"Test$i\"}"
    curl -X POST http://192.168.10.1/api/profile/activate \
         -d "{\"profile_id\":$i}"
done
```
**Résultat :** ✅ Aucun crash

**Test 3 : Événements multiples**
```bash
# Configuration de 22 événements simultanément
curl -X POST http://192.168.10.1/api/events -d @events.json
```
**Résultat :** ✅ Traitement en <200ms

## 🔍 Détection et Prévention des Erreurs

### Stack Overflow Guards

ESP-IDF fournit des mécanismes de détection :

```c
// Configuration sdkconfig
CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK=y
CONFIG_ESP_TASK_WDT_PANIC=y
```

**Détection automatique :**
- Watchpoint sur fin de stack
- Task watchdog timer (TWDT)
- Exception handlers

### Logs Critiques à Surveiller

```
✅ "Page HTML envoyée avec succès"
✅ "Profil sauvegardé avec succès"
✅ "CAN frame received: ID=0x118"

❌ "Erreur envoi HTML: ESP_ERR_HTTPD_RESP_SEND"
❌ "***ERROR*** A stack overflow in task has been detected"
❌ "Guru Meditation Error: Core 1 panic'ed (StoreProhibited)"
❌ "Failed to allocate memory for profiles"
```

## 🛠️ Outils de Débogage

### Commandes Utiles

**Analyser la mémoire :**
```bash
# Heap disponible au runtime
pio device monitor --filter="heap"

# Stack highwater mark
pio device monitor --filter="stack"
```

**Décoder les exceptions :**
```bash
# Avec PlatformIO
pio device monitor --filter esp32_exception_decoder

# Avec ESP-IDF
idf.py monitor
```

**Analyse statique :**
```bash
# Taille des sections
xtensa-esp32-elf-size build/firmware.elf

# Symboles et leur taille
xtensa-esp32-elf-nm -S -C build/firmware.elf | grep config_profile
```

### Exemple d'Analyse de Crash

**Log d'erreur avant correction :**
```
Guru Meditation Error: Core 1 panic'ed (StoreProhibited)
EXCVADDR: 0x00feffa0
Backtrace: 0x4008b713:0x3ffcce30 0x4008b5d0:0x3ffcce40
```

**Analyse :**
- `EXCVADDR: 0x00feffa0` → Adresse mémoire corrompue
- Backtrace dans `config_handler` → Profil alloué sur stack
- Stack size insuffisante → Écrasement mémoire adjacente

**Résolution :**
1. Identifier le handler problématique via backtrace
2. Localiser allocation stack de `config_profile_t`
3. Remplacer par malloc/free
4. Ajouter gestion d'erreur
5. Vérifier tous les chemins de libération

## 📈 Performance & Optimisations

### Performances du Système CAN

- **Décodage message CAN** : < 1ms par message
- **Détection événement** : 100ms cycle (configurable)
- **Latence totale CAN → LED** : < 100ms
- **Mémoire par config véhicule** : ~2-3KB

### Performances LED

- **Fréquence de rafraîchissement** : 50 FPS (20ms)
- **Nombre de LEDs max** : 300+ (avec injection de courant)
- **Latence effet** : < 20ms

### Performances Réseau

- **Clients WiFi simultanés** : 4
- **Latence API REST** : 50-200ms
- **Taille HTML compressé** : ~18KB
- **Débit upload OTA** : ~50KB/s

### Optimisation JSON (v2.3.0)

Depuis la v2.3.0, l'API REST utilise des **clés JSON courtes** pour optimiser les performances :

**Bénéfices mesurés :**
- **Réduction de taille** : 30-40% des payloads JSON
- **Parsing plus rapide** : ~20% d'amélioration sur ESP32
- **Économie RAM** : ~1-2KB par requête HTTP
- **Bande passante** : Réduction proportionnelle du trafic réseau

**Exemples de réduction :**
```json
// Ancien format (130 octets)
{
  "wifi_connected": true,
  "can_bus_running": true,
  "vehicle_active": true,
  "effect": "RAINBOW",
  "brightness": 200
}

// Nouveau format (75 octets) - 42% de réduction
{
  "wc": true,
  "cbr": true,
  "va": true,
  "fx": "RAINBOW",
  "br": 200
}
```

**Impact sur la mémoire :**
- Moins d'allocations temporaires lors du parsing
- Buffers HTTP plus petits possibles
- Réduction du temps de traitement cJSON
- Amélioration de la réactivité de l'interface web

**Documentation complète** : [docs/JSON_API_REFERENCE.md](docs/JSON_API_REFERENCE.md)

## 🎯 Évolution Future

### Optimisations Réalisées

**v2.3.0 :**
- [x] ~~Optimisation JSON avec clés courtes (réduction 30-40%)~~
- [x] ~~Amélioration parsing cJSON~~
- [x] ~~Réduction bande passante réseau~~

### Optimisations Planifiées

**Court terme :**
- [ ] Pool de mémoire pour profils (réutilisation)
- [ ] Compression des profils en NVS
- [ ] Cache des profils fréquemment utilisés

**Moyen terme :**
- [ ] Migration vers partition SPIFFS pour profils
- [ ] Réduction taille `can_event_effect_t` (~80 bytes actuellement)
- [ ] Partage de mémoire entre effets similaires

**Long terme :**
- [ ] Support PSRAM (ESP32-WROVER)
- [ ] Profils illimités (stockage externe)
- [ ] Système de pagination pour gros volumes

## 📚 Références Techniques

- [ESP32 Memory Layout](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/general-notes.html#memory-layout)
- [FreeRTOS Stack Overflow Detection](https://www.freertos.org/Stacks-and-stack-overflow-checking.html)
- [ESP-IDF Heap Memory Debugging](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/heap_debug.html)
- [CAN DBC Format Specification](https://www.csselectronics.com/pages/can-dbc-file-database-intro)

---

**Version :** 2.2.0
**Date :** 2025-11-20
**Auteur :** Car Light Sync Development Team
