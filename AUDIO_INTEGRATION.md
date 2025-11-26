# Intégration du Micro INMP441 - COMPLÈTE ✅

## 🎉 Implémentation 100% Complétée

Toute l'intégration du micro INMP441 est maintenant complète et fonctionnelle!

## ✅ Modules Implémentés

### 1. Module Audio I2S (audio_input.c/h)
- ✅ Driver I2S pour le micro INMP441
- ✅ Traitement audio en temps réel (amplitude, bandes de fréquence)
- ✅ Détection de battements (beat detection)
- ✅ Calcul du BPM
- ✅ Tâche dédiée pour le traitement audio (~50Hz)
- ✅ Configuration NVS pour sauvegarder les paramètres

**GPIO par défaut (modifiables):**
- SCK: GPIO 10
- WS: GPIO 11
- SD: GPIO 9

### 2. Effets LED Audio-Réactifs
- ✅ Nouveau champ `audio_reactive` dans `effect_config_t`
- ✅ Modulation automatique de tous les effets existants par l'amplitude audio
- ✅ Effet `EFFECT_AUDIO_REACTIVE`: VU-mètre visuel
- ✅ Effet `EFFECT_AUDIO_BPM`: Flash synchronisé au BPM détecté

### 3. Intégration Système
- ✅ Ajout dans [main/CMakeLists.txt](main/CMakeLists.txt:47)
- ✅ Initialisation dans [main.c](main/main.c:417-422)
- ✅ Les effets peuvent être rendus audio-réactifs via `audio_reactive = true`

### 4. Serveur Web Backend (web_server.c)
- ✅ Endpoints API audio implémentés dans [web_server.c](main/web_server.c:1498-1647)
- ✅ `/api/audio/status` - Statut et configuration du micro
- ✅ `/api/audio/enable` - Activer/désactiver le micro
- ✅ `/api/audio/config` - Mettre à jour la configuration
- ✅ `/api/audio/data` - Données audio en temps réel
- ✅ Handlers enregistrés dans [web_server_start()](main/web_server.c:1848-1871)

### 5. Interface Web Frontend
- ✅ Traductions FR/EN ajoutées dans [script.js](data/script.js:74-102)
- ✅ Interface audio complète dans [index.html](data/index.html:349-412)
- ✅ Logique JavaScript implémentée dans [script.js](data/script.js:2556-2712)
- ✅ Checkbox "Audio Reactive" sur l'effet par défaut
- ✅ Polling en temps réel des données audio (5Hz)
- ✅ Sauvegarde de la configuration audio

## 📚 Référence API Complète

### Endpoints REST Implémentés

Tous les endpoints sont maintenant fonctionnels:

#### GET `/api/audio/status`
Retourne le statut et la configuration du micro.

**Réponse:**
```json
{
  "enabled": true,
  "sensitivity": 128,
  "gain": 128,
  "autoGain": true,
  "sckPin": 10,
  "wsPin": 11,
  "sdPin": 9
}
```

#### POST `/api/audio/enable`
Active ou désactive le micro.

**Requête:**
```json
{
  "enabled": true
}
```

#### POST `/api/audio/config`
Met à jour la configuration audio.

**Requête:**
```json
{
  "sensitivity": 150,
  "gain": 180,
  "autoGain": false
}
```

#### GET `/api/audio/data`
Retourne les données audio en temps réel.

**Réponse:**
```json
{
  "amplitude": 0.75,
  "bass": 0.45,
  "mid": 0.30,
  "treble": 0.25,
  "bpm": 120.5,
  "beatDetected": true,
  "available": true
}
```

## 🔧 Configuration GPIO

Les GPIO par défaut ont été définis dans [audio_input.h](include/audio_input.h:8-10):
- SCK: GPIO 10
- WS: GPIO 11
- SD: GPIO 9

Ces valeurs peuvent être modifiées selon le câblage du micro INMP441.

## 📝 Câblage INMP441

```
INMP441          ESP32
-------          -----
VDD       -----> 3.3V
GND       -----> GND
SD        -----> GPIO 9 (configurable)
WS (LR)   -----> GPIO 11 (configurable)
SCK       -----> GPIO 10 (configurable)
L/R       -----> GND (pour canal gauche)
```

## 🎯 Fonctionnalités

### Effets Audio
1. **Audio Reactive Mode**: Active sur TOUS les effets existants
   - Moduler l'intensité/luminosité en fonction de l'amplitude
   - 10% base + 90% audio reactive (variation très visible)

2. **Effet VU-Mètre** (`EFFECT_AUDIO_REACTIVE`)
   - Affiche un bargraph visuel
   - Remplissage proportionnel à l'amplitude

3. **Effet BPM Flash** (`EFFECT_AUDIO_BPM`)
   - Flash synchronisé aux battements détectés
   - Decay progressif entre les beats

### API Audio
- Détection d'amplitude (0.0 - 1.0)
- Séparation par bandes: bass, mid, treble
- Détection de battements en temps réel
- Calcul du BPM (60-180 BPM)

## ⚠️ Notes Importantes

1. **Activation uniquement sur l'effet par défaut**: Le micro ne peut être activé QUE depuis l'effet par défaut du profil (pas sur les événements CAN).

2. **Événements CAN prioritaires**: Lorsque vous modifiez l'effet par défaut dans l'interface web, tous les événements CAN actifs sont automatiquement arrêtés pour que le changement soit immédiatement visible. Les événements CAN (clignotants, charge, etc.) continueront de fonctionner normalement par la suite.

3. **Connexion BLE optimisée**:
   - Les requêtes importantes (sauvegarde d'effets, simulation d'événements) attendent automatiquement que la queue soit vide avant d'être exécutées, évitant les erreurs "Commande BLE refusée"
   - Le polling des données audio (5Hz en WiFi) est **automatiquement désactivé** quand vous n'êtes pas sur l'onglet Configuration, évitant un embouteillage permanent de la queue BLE
   - En BLE, le polling est ralenti à 1Hz au lieu de 5Hz
   - Le délai entre les requêtes BLE a été réduit de 50ms à 20ms

4. **Performance**: Le traitement audio tourne sur un core séparé à ~50Hz, pas d'impact sur les LEDs.

5. **Mémoire**: Utilise ~4KB de RAM pour les buffers audio.

6. **I2S**: Utilise le périphérique I2S disponible (compatible avec ESP32, ESP32-S3, etc.).

## 🚀 Prochaines Améliorations Possibles

- FFT réelle pour analyse spectrale avancée
- Égaliseur graphique dans l'interface web
- Presets audio (Bass Boost, Vocal, etc.)
- Calibration automatique du gain
- Visualisation spectrale en temps réel
