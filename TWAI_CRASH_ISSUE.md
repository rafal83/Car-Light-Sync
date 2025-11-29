# Problème: Crash TWAI quand le bus CAN n'est pas connecté

## ⚠️ CAUSE RACINE IDENTIFIÉE

**Le code ne gère PAS les alertes TWAI !**

### Analyse du code `main/can_bus.c`

#### Ligne 111-118 (can_bus_init) :
```c
twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
    tx_gpio, rx_gpio, TWAI_MODE_LISTEN_ONLY);
```

**Problèmes** :
❌ `g_config.alerts_enabled` n'est PAS configuré (= 0 par défaut)
❌ Aucune tâche pour surveiller les alertes TWAI
❌ Pas de récupération automatique du bus_off

### Séquence du crash

1. **Initialisation** : Le driver TWAI démarre sans alertes configurées
2. **Sans bus physique** : Le contrôleur ne peut pas arbitrer le bus
3. **Accumulation d'erreurs** : Compteur d'erreur TX/RX augmente
4. **État BUS_OFF** : Après 256 erreurs, passage en BUS_OFF
5. **Interruptions en boucle** : Le handler `twai_intr_handler_main` tourne sans cesse
6. **Crash** : Corruption mémoire ou watchdog timeout

## Stack Trace

```
0x00000000: ?? ??:0
0x400559cf: ?? ??:0
0x40382b0d: vPortClearInterruptMaskFromISR at FreeRTOS/portmacro.h:560
 (inlined by) vPortExitCritical at FreeRTOS/port.c:514
0x420218ef: twai_intr_handler_main at driver/twai/twai.c:266
0x4037f20d: _xt_lowint1 at xtensa_vectors.S:1240
0x42021910: twai_intr_handler_main at driver/twai/twai.c:275
0x42030215: esp_vApplicationIdleHook at esp_system/freertos_hooks.c:58
0x4038364f: prvIdleTask at FreeRTOS/tasks.c:4353 (discriminator 1)
0x40382859: vPortTaskWrapper at FreeRTOS/port.c:139
```

## Solution complète

Voir le fichier `CAN_BUS_FIX.patch` pour les modifications complètes.

### Modifications critiques à appliquer

#### 1. Activer les alertes TWAI (lignes 111-120)

```c
twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
    tx_gpio, rx_gpio, TWAI_MODE_LISTEN_ONLY);

// CRITIQUE: Activer les alertes pour détecter bus_off et erreurs
g_config.alerts_enabled = TWAI_ALERT_BUS_OFF |
                          TWAI_ALERT_ERR_PASS |
                          TWAI_ALERT_BUS_RECOVERED |
                          TWAI_ALERT_ABOVE_ERR_WARN |
                          TWAI_ALERT_ERR_ACTIVE;

// Permet la récupération automatique depuis bus_off
g_config.bus_off_io = 1;
```

#### 2. Créer une tâche de monitoring des alertes

```c
static void can_alert_task(void *pvParameters) {
  can_bus_type_t bus_type = *((can_bus_type_t*)pvParameters);
  can_bus_context_t *ctx = &s_can_buses[bus_type];
  const char *bus_name = (bus_type == CAN_BUS_BODY) ? "BODY" : "CHASSIS";

  while (ctx->running) {
    uint32_t alerts;
    esp_err_t ret = twai_read_alerts(&alerts, pdMS_TO_TICKS(1000));

    if (ret == ESP_OK && (alerts & TWAI_ALERT_BUS_OFF)) {
      ESP_LOGW(TAG_CAN_BUS, "[%s] BUS_OFF - Récupération...", bus_name);
      twai_initiate_recovery();  // Lance la récupération auto
    }
  }
  vTaskDelete(NULL);
}
```

#### 3. Lancer la tâche de monitoring au démarrage (dans can_bus_start)

```c
// Après création de la tâche RX
xTaskCreatePinnedToCore(can_alert_task, "can_alert", 2048,
                        alert_params, 9, NULL, 0);
```

## Bénéfices de la solution

✅ **Plus de crash** : Les alertes sont gérées proprement
✅ **Récupération auto** : Le bus se remet automatiquement du bus_off
✅ **Logs informatifs** : On sait pourquoi le bus ne fonctionne pas
✅ **Mode test possible** : L'ESP32 peut tourner sans bus CAN connecté

## Impact

**Avant** :
- Crash complet de l'ESP32 → redémarrage nécessaire
- Perte de connexion avec l'interface web
- Impossibilité d'utiliser le module sans bus CAN connecté

**Après** :
- Module stable même sans bus CAN
- Logs clairs sur l'état du bus
- Récupération automatique quand le bus revient

## Tests recommandés

1. ✅ Démarrer sans bus CAN connecté → Doit logger "BUS_OFF" sans crash
2. ✅ Connecter le bus pendant que l'ESP32 tourne → Doit récupérer automatiquement
3. ✅ Déconnecter le bus CAN en fonctionnement → Doit passer en bus_off proprement

## Priorité

**CRITIQUE** - Le module doit pouvoir fonctionner sans bus CAN connecté (mode test, développement).

## Fichiers modifiés

- `main/can_bus.c` : Activation alertes + tâche de monitoring
- Voir `CAN_BUS_FIX.patch` pour le diff complet
