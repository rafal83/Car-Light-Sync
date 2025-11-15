# Nouvelles Fonctionnalités - Tesla Strip Controller v2.0

## 🆕 Nouveautés

### Connexion Commander S3XY_OBD

Le système se connecte maintenant directement au Commander avec les paramètres suivants :
- **SSID**: `S3XY_OBD`
- **Mot de passe**: `12345678`
- **Adresse IP**: `192.168.4.1:1338`

### Messages CAN Ajoutés

#### Blindspot (Détection Angle Mort)
- **ID CAN**: `0x2A5`
- **Événements**:
  - `CAN_EVENT_BLINDSPOT_LEFT` : Détection angle mort gauche
  - `CAN_EVENT_BLINDSPOT_RIGHT` : Détection angle mort droite
- **Animation par défaut**: Strobe rouge à priorité maximale

#### Mode Nuit Automatique
- **ID CAN**: `0x3C8`
- **Événements**:
  - `CAN_EVENT_NIGHT_MODE_ON` : Mode nuit activé (capteur luminosité)
  - `CAN_EVENT_NIGHT_MODE_OFF` : Mode nuit désactivé
- **Comportement**: Réduit automatiquement la luminosité et change l'effet

## 🎨 Système de Profils de Configuration

### Gestion des Profils

Le système supporte maintenant **jusqu'à 10 profils** de configuration différents, chacun avec :

- Nom personnalisé
- Effet par défaut
- Effet mode nuit
- Configuration spécifique par événement CAN
- Paramètres de luminosité et vitesse
- Mode nuit automatique

### Structure d'un Profil

```c
typedef struct {
    char name[32];                      // Nom du profil
    effect_config_t default_effect;     // Effet par défaut
    effect_config_t night_mode_effect;  // Effet en mode nuit
    can_event_effect_t event_effects[]; // Effets par événement (17 événements)
    bool auto_night_mode;               // Active auto le mode nuit
    uint8_t night_brightness;           // Luminosité mode nuit (0-255)
    uint16_t speed_threshold;           // Seuil vitesse (km/h)
} config_profile_t;
```

### Création de Profils

#### Via l'Interface Web

1. Cliquer sur "Nouveau" dans la section Profils
2. Entrer un nom pour le profil
3. Configurer les effets par défaut
4. Assigner des effets aux événements CAN
5. Le profil est automatiquement sauvegardé en NVS

#### Via API REST

```bash
# Créer un nouveau profil
curl -X POST http://192.168.4.1/api/profile/create \
  -H "Content-Type: application/json" \
  -d '{"name": "Sport Mode"}'

# Activer un profil
curl -X POST http://192.168.4.1/api/profile/activate \
  -H "Content-Type: application/json" \
  -d '{"profile_id": 1}'

# Supprimer un profil
curl -X POST http://192.168.4.1/api/profile/delete \
  -H "Content-Type: application/json" \
  -d '{"profile_id": 2}'
```

## 🎯 Association Événements CAN → Effets

### Configuration des Événements

Chaque événement CAN peut avoir un effet LED personnalisé avec :

- **Effet LED** : Type d'animation (Rainbow, Strobe, Breathing, etc.)
- **Luminosité** : 0-255
- **Vitesse** : 0-255
- **Couleur(s)** : RGB en hexadécimal
- **Durée** : Millisecondes (0 = permanent)
- **Priorité** : 0-255 (plus élevé = prioritaire)

### Système de Priorité

Lorsque plusieurs événements se produisent simultanément :

1. L'effet avec la **priorité la plus élevée** s'affiche
2. Les effets temporaires retournent à l'effet par défaut après leur durée
3. Les effets permanents restent actifs jusqu'au prochain événement

### Exemple de Configuration

```json
{
  "event": "BLINDSPOT_LEFT",
  "effect": "STROBE",
  "duration": 0,
  "priority": 250,
  "brightness": 255,
  "speed": 200,
  "color": 16711680  // Rouge (0xFF0000)
}
```

## 🌙 Mode Nuit Automatique

### Fonctionnement

Le mode nuit s'active automatiquement quand :
- Le message CAN `0x3C8` indique une faible luminosité ambiante
- Le paramètre `auto_night_mode` est activé dans le profil

### Configuration

```json
{
  "auto_night_mode": true,
  "night_brightness": 30,  // Luminosité réduite
  "night_mode_effect": {
    "effect": 2,           // Breathing
    "brightness": 30,
    "speed": 20,
    "color1": 255          // Bleu doux
  }
}
```

### Comportement

1. **Activation** : Passage automatique à l'effet mode nuit
2. **Désactivation** : Retour à l'effet par défaut du profil
3. **Priorité** : Les événements CAN prioritaires peuvent override le mode nuit

## 🎨 Effets LED Disponibles

| ID String | Nom | Description |
|-----------|-----|-------------|
| `OFF` | Off | LEDs éteintes |
| `SOLID` | Solid | Couleur unie statique |
| `BREATHING` | Breathing | Respiration douce |
| `RAINBOW` | Rainbow | Arc-en-ciel statique |
| `RAINBOW_CYCLE` | Rainbow Cycle | Arc-en-ciel qui défile |
| `THEATER_CHASE` | Theater Chase | Effet théâtre |
| `RUNNING_LIGHTS` | Running Lights | Lumières qui courent |
| `TWINKLE` | Twinkle | Scintillement |
| `FIRE` | Fire | Simulation feu |
| `SCAN` | Scan | Balayage type K2000 |
| `KNIGHT_RIDER` | Knight Rider | K2000 classique |
| `FADE` | Fade | Fondu progressif |
| `STROBE` | Strobe | Stroboscope |
| `VEHICLE_SYNC` | Vehicle Sync | Synchronisé véhicule |
| `TURN_SIGNAL` | Turn Signal | Clignotant animé |
| `BRAKE_LIGHT` | Brake Light | Feu de freinage |
| `CHARGE_STATUS` | Charge Status | Indicateur de charge |
| `HAZARD` | Hazard | Warning animé |
| `BLINDSPOT_FLASH` | Blindspot Flash | Flash angle mort |

## 📊 Événements CAN Disponibles

| ID String | Événement | Déclencheur | Priorité Suggérée |
|-----------|-----------|-------------|-------------------|
| `TURN_LEFT` | Turn Left | Clignotant gauche actif | 200 |
| `TURN_RIGHT` | Turn Right | Clignotant droit actif | 200 |
| `TURN_HAZARD` | Turn Hazard | Warning activé | 220 |
| `CHARGING` | Charging | Début de charge | 150 |
| `CHARGE_COMPLETE` | Charge Complete | Charge ≥ 80% terminée | 140 |
| `DOOR_OPEN` | Door Open | Ouverture d'une porte | 100 |
| `DOOR_CLOSE` | Door Close | Fermeture portes | 90 |
| `LOCKED` | Locked | Véhicule verrouillé | 110 |
| `UNLOCKED` | Unlocked | Véhicule déverrouillé | 110 |
| `BRAKE_ON` | Brake On | Frein appuyé | 180 |
| `BRAKE_OFF` | Brake Off | Frein relâché | 170 |
| `BLINDSPOT_LEFT` | Blindspot Left | Angle mort gauche détecté | 250 |
| `BLINDSPOT_RIGHT` | Blindspot Right | Angle mort droit détecté | 250 |
| `NIGHT_MODE_ON` | Night Mode On | Mode nuit activé | 0 |
| `NIGHT_MODE_OFF` | Night Mode Off | Mode nuit désactivé | 0 |
| `SPEED_THRESHOLD` | Speed Threshold | Vitesse > seuil | 60 |

## 🎬 Exemples de Profils

### Profil "Sport"

```c
// Effet par défaut : Rainbow rapide
default_effect = {
    .effect = EFFECT_RAINBOW,
    .brightness = 200,
    .speed = 150,
};

// Clignotants : Strobe orange agressif
event_effects[CAN_EVENT_TURN_LEFT] = {
    .effect = EFFECT_STROBE,
    .brightness = 255,
    .speed = 255,
    .color1 = 0xFF8000,
    .priority = 200,
    .duration_ms = 0
};

// Blindspot : Flash rouge intense
event_effects[CAN_EVENT_BLINDSPOT_LEFT] = {
    .effect = EFFECT_STROBE,
    .brightness = 255,
    .speed = 255,
    .color1 = 0xFF0000,
    .priority = 250,
    .duration_ms = 0
};
```

### Profil "Discret"

```c
// Effet par défaut : Breathing doux
default_effect = {
    .effect = EFFECT_BREATHING,
    .brightness = 80,
    .speed = 30,
    .color1 = 0xFFFFFF,  // Blanc
};

// Mode nuit : Très doux
night_mode_effect = {
    .effect = EFFECT_BREATHING,
    .brightness = 20,
    .speed = 15,
    .color1 = 0x0000FF,  // Bleu
};

auto_night_mode = true;
night_brightness = 20;
```

### Profil "Sécurité"

```c
// Priorité aux alertes de sécurité

// Blindspot : Priorité maximale
event_effects[CAN_EVENT_BLINDSPOT_LEFT] = {
    .effect = EFFECT_STROBE,
    .brightness = 255,
    .speed = 255,
    .color1 = 0xFF0000,
    .priority = 255,
    .duration_ms = 0
};

// Porte ouverte déverrouillée : Alerte
event_effects[CAN_EVENT_DOOR_OPEN] = {
    .effect = EFFECT_STROBE,
    .brightness = 200,
    .speed = 150,
    .color1 = 0xFF6600,
    .priority = 220,
    .duration_ms = 5000  // 5 secondes
};
```

## 🔧 API REST Complète

### Profils

```bash
# Lister tous les profils
GET /api/profiles

# Activer un profil
POST /api/profile/activate
Body: {"profile_id": 1}

# Créer un profil
POST /api/profile/create
Body: {"name": "Mon Profil"}

# Supprimer un profil
POST /api/profile/delete
Body: {"profile_id": 2}
```

### Effets et Événements

```bash
# Lister tous les effets disponibles
GET /api/effects

# Lister tous les types d'événements
GET /api/event-types

# Obtenir la configuration de tous les événements
GET /api/events

# Mettre à jour la configuration des événements
POST /api/events
Body: {
  "events": [
    {
      "event": "TURN_LEFT",
      "effect": "KNIGHT_RIDER",
      "brightness": 200,
      "speed": 200,
      "color": 16744448,
      "duration": 0,
      "priority": 200,
      "enabled": true
    },
    {
      "event": "BLINDSPOT_LEFT",
      "effect": "BLINDSPOT_FLASH",
      "brightness": 255,
      "speed": 250,
      "color": 16711680,
      "duration": 0,
      "priority": 250,
      "enabled": true
    }
  ]
}

# Mettre à jour l'effet par défaut d'un profil
POST /api/profile/update/default
Body: {
  "profile_id": 0,
  "effect": 2,  // ID numérique de l'effet
  "brightness": 150,
  "speed": 80,
  "color1": 16711680
}
```

### Statut

```bash
# Obtenir l'état complet
GET /api/status

# Réponse:
{
  "wifi_connected": true,
  "commander_connected": true,
  "vehicle_active": true,
  "vehicle": {
    "speed": 45.2,
    "gear": 3,
    "charge": 78,
    "doors_open": 0,
    "night_mode": false,
    "blindspot_left": false,
    "blindspot_right": true
  }
}
```

## 💾 Sauvegarde et Persistance

### Stockage NVS

Tous les profils sont sauvegardés dans la mémoire non-volatile (NVS) :
- **Partition**: `profiles`
- **Format**: Binaire (struct config_profile_t)
- **Clés**: `profile_0` à `profile_9`, `active_id`

### Import/Export

```bash
# Exporter un profil (à venir)
GET /api/profile/export?id=1

# Importer un profil (à venir)
POST /api/profile/import
Body: {JSON du profil}
```

## 🚀 Utilisation Avancée

### Scénarios Multi-Profils

**Scénario 1: Profils Jour/Nuit**
- Profil "Jour" avec animations vives
- Profil "Nuit" avec mode auto et luminosité réduite
- Switch automatique via mode nuit CAN

**Scénario 2: Profils par Usage**
- "Ville" : Effets discrets, priorité sécurité
- "Autoroute" : Effets dynamiques, blindspot actif
- "Parking" : Effets statiques, alertes portes

**Scénario 3: Profils Personnalisés**
- "Fête" : Rainbow intense, pas d'auto night
- "Romantique" : Breathing rose doux
- "Sportif" : Effets agressifs, réactifs

### Triggers Conditionnels

Combiner vitesse et événements :

```c
// Effet différent selon la vitesse
if (speed > 80) {
    // Autoroute : animations rapides
    profile->default_effect.speed = 200;
} else {
    // Ville : animations lentes
    profile->default_effect.speed = 50;
}
```

## 📝 Notes Techniques

### Performance

- Vérification événements : 100ms
- Mise à jour LEDs : 20ms (50 FPS)
- Latence CAN → LED : < 150ms
- Mémoire par profil : ~1KB

### Limites

- Maximum 10 profils simultanés
- Maximum 22 types d'événements CAN
- Durée max effet temporaire : 60 secondes
- Priorité 0-255
- Taille profil en mémoire : ~1900 bytes
- Mémoire totale profils : ~19KB (10 × 1900 bytes)

## 🛡️ Optimisations et Stabilité

### Gestion de la Mémoire (v2.1.0)

Le système utilise une allocation dynamique intelligente pour éviter les stack overflows:

**Problème résolu:**
- Les structures `config_profile_t` (~1900 bytes) causaient des stack overflows lorsqu'allouées sur la stack
- Les handlers HTTP avec plusieurs profils (10 × 1900 = 19KB) dépassaient la limite de la stack

**Solution implémentée:**
```c
// Avant (stack overflow)
config_profile_t profile;  // 1900 bytes sur la stack !

// Après (stable)
config_profile_t *profile = malloc(sizeof(config_profile_t));
if (profile != NULL) {
    // Utilisation sécurisée
    free(profile);
}
```

**Handlers optimisés:**
- `config_handler()` : Allocation dynamique du profil actif
- `profiles_handler()` : Allocation de l'array de 10 profils sur le heap
- `profile_update_handler()` : Allocation dynamique pour modifications
- `profile_update_default_handler()` : Allocation dynamique pour mises à jour
- `event_effect_handler()` : Allocation temporaire pour sauvegarde
- `events_post_handler()` : Allocation pour traitement batch

**Configuration HTTP optimisée:**
- Stack size augmentée à 16KB (au lieu de 12KB)
- Timeouts augmentés à 30s (au lieu de 10s)
- Gestion d'erreur complète avec libération mémoire

**Résultat:**
- ✅ Stabilité accrue - Plus de Guru Meditation Errors
- ✅ Interface web 100% fonctionnelle
- ✅ Gestion de 10 profils sans problème
- ✅ Utilisation RAM optimisée : 14.5%
- ✅ Utilisation Flash : 49.6%

### Messages CAN Étendus

**Nouveaux événements v2.1.0:**
- `AUTOPILOT_ENGAGED` : Autopilot Tesla activé
- `AUTOPILOT_DISENGAGED` : Autopilot désactivé
- `GEAR_DRIVE` : Passage en mode Drive (D)
- `GEAR_REVERSE` : Passage en marche arrière (R)
- `GEAR_PARK` : Passage en mode Park (P)

**Total événements supportés : 22**

---

Pour plus d'informations, consultez le README principal et ADVANCED.md.
