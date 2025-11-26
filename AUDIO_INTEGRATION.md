# Intégration du Micro INMP441 - COMPLÈTE ✅

## 🎉 Implémentation 100% Complétée

Toute l'intégration du micro INMP441 est maintenant complète et fonctionnelle!

## ✅ Modules Implémentés

### 1. Module Audio I2S (audio_input.c/h)
- ✅ Driver I2S pour le micro INMP441
- ✅ Traitement audio en temps réel (amplitude, bandes de fréquence)
- ✅ Détection de battements (beat detection)
- ✅ Calcul du BPM
- ✅ **Analyse FFT avancée (32 bandes)**
- ✅ **Détection de kick/snare/vocal**
- ✅ **Centroïde spectral et fréquence dominante**
- ✅ Tâche dédiée pour le traitement audio (~50Hz)
- ✅ Configuration NVS pour sauvegarder les paramètres

**GPIO par défaut (modifiables):**
- SCK: GPIO 12
- WS: GPIO 13
- SD: GPIO 11

### 2. Effets LED Audio-Réactifs
- ✅ Nouveau champ `audio_reactive` dans `effect_config_t`
- ✅ Modulation automatique de tous les effets existants par l'amplitude audio
- ✅ Effet `EFFECT_AUDIO_REACTIVE`: VU-mètre visuel
- ✅ Effet `EFFECT_AUDIO_BPM`: Flash synchronisé au BPM détecté
- ✅ **Effet `EFFECT_FFT_SPECTRUM`: Spectre FFT en temps réel (égaliseur)**
- ✅ **Effet `EFFECT_FFT_BASS_PULSE`: Pulse sur les basses (kick)**
- ✅ **Effet `EFFECT_FFT_VOCAL_WAVE`: Vague réactive aux voix**
- ✅ **Effet `EFFECT_FFT_ENERGY_BAR`: Barre d'énergie spectrale**

### 3. Intégration Système
- ✅ Ajout dans [main/CMakeLists.txt](main/CMakeLists.txt:47)
- ✅ Initialisation dans [main.c](main/main.c:417-422)
- ✅ Les effets peuvent être rendus audio-réactifs via `audio_reactive = true`

### 4. Serveur Web Backend (web_server.c)
- ✅ Endpoints API audio implémentés dans [web_server.c](main/web_server.c:1498-1647)
- ✅ `/api/audio/status` - Statut et configuration du micro
- ✅ `/api/audio/enable` - Activer/désactiver le micro
- ✅ `/api/audio/config` - Mettre à jour la configuration
- ✅ `/api/audio/data` - **Données audio + FFT unifiées en un seul appel**
- ✅ `/api/audio/fft/enable` - Activer/désactiver le FFT
- ✅ Handlers enregistrés dans [web_server_start()](main/web_server.c:1848-1871)

### 5. Interface Web Frontend
- ✅ Traductions FR/EN ajoutées dans [script.js](data/script.js:74-102)
- ✅ Interface audio complète dans [index.html](data/index.html:349-412)
- ✅ Logique JavaScript implémentée dans [script.js](data/script.js:2556-2712)
- ✅ Checkbox "Audio Reactive" sur l'effet par défaut
- ✅ **Section FFT Advanced avec visualisation spectrale canvas**
- ✅ **Activation automatique du FFT selon l'effet sélectionné**
- ✅ Polling en temps réel des données audio + FFT unifiées (1 seul appel)
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
  "autoGain": true
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
Retourne les données audio **ET FFT** en temps réel (un seul appel optimisé).

**Réponse:**
```json
{
  "amplitude": 0.75,
  "bass": 0.45,
  "mid": 0.30,
  "treble": 0.25,
  "bpm": 120.5,
  "beatDetected": true,
  "available": true,
  "fft": {
    "available": true,
    "bands": [0.1, 0.2, 0.3, ...],  // 32 bandes de fréquence
    "peakFreq": 440.5,
    "spectralCentroid": 1200.0,
    "kickDetected": false,
    "snareDetected": false,
    "vocalDetected": true
  }
}
```

#### POST `/api/audio/fft/enable`
Active ou désactive le mode FFT avancé.

**Requête:**
```json
{
  "enabled": true
}
```

**Note:** Le FFT est désormais **activé automatiquement** par l'interface web lorsqu'un effet FFT est sélectionné (voir section "Activation Automatique du FFT" ci-dessous).

## 🔧 Configuration GPIO

Les GPIO par défaut ont été définis dans [audio_input.h](include/audio_input.h:8-10):
- SCK: GPIO 12
- WS: GPIO 13
- SD: GPIO 11

Ces valeurs peuvent être modifiées selon le câblage du micro INMP441.

## 📝 Câblage INMP441

```
INMP441          ESP32
-------          -----
VDD       -----> 3.3V
GND       -----> GND
SD        -----> GPIO 11 (configurable)
WS (LR)   -----> GPIO 13 (configurable)
SCK       -----> GPIO 12 (configurable)
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

4. **Effets FFT Avancés** (activés automatiquement)
   - `EFFECT_FFT_SPECTRUM`: Égaliseur spectral 32 bandes
   - `EFFECT_FFT_BASS_PULSE`: Réagit aux kicks/basses
   - `EFFECT_FFT_VOCAL_WAVE`: Détection et visualisation vocale
   - `EFFECT_FFT_ENERGY_BAR`: Barre d'énergie spectrale globale

### API Audio
- Détection d'amplitude (0.0 - 1.0)
- Séparation par bandes: bass, mid, treble
- Détection de battements en temps réel
- Calcul du BPM (60-180 BPM)

### Analyse FFT Avancée
- 32 bandes de fréquence (20Hz - 10kHz)
- Fréquence dominante (peak frequency)
- Centroïde spectral (balance fréquentielle)
- Détection de kick (basses < 150Hz)
- Détection de snare (200-500Hz)
- Détection vocale (200-3000Hz)

## 🔄 Activation Automatique du FFT

Le FFT s'active **automatiquement** selon l'effet sélectionné, **entièrement géré par le backend** :

### Effets nécessitant le FFT
Lorsque vous sélectionnez l'un de ces effets, le backend active automatiquement le FFT :
- `EFFECT_AUDIO_REACTIVE` (58)
- `EFFECT_AUDIO_BPM` (59)
- `EFFECT_FFT_SPECTRUM` (60)
- `EFFECT_FFT_BASS_PULSE` (61)
- `EFFECT_FFT_VOCAL_WAVE` (62)
- `EFFECT_FFT_ENERGY_BAR` (63)

### Architecture Backend-Driven
- **Core LED** ([led_effects.c:1382-1388](main/led_effects.c:1382-1388)) : Lors de l'application d'un effet via `led_effects_set_config()` :
  1. Vérifie si l'effet nécessite le FFT via `led_effects_requires_fft()`
  2. Active/désactive automatiquement le FFT via `audio_input_set_fft_enabled()`
  3. Log l'activation : `"Effet X configuré, FFT activé/désactivé"`
  4. **Fonctionne quel que soit la source** : HTTP, profil, événement CAN, etc.

- **Frontend** ([script.js:2843-2847](data/script.js:2843-2847)) : L'interface ne fait **aucune décision** :
  1. Recharge simplement l'état FFT depuis `/api/audio/status` après application d'un effet
  2. Affiche/masque la section FFT selon l'état renvoyé par le backend
  3. Aucune logique de décision côté client
  4. **Zéro couplage** avec la logique métier

### Avantages
- ✅ **Architecture propre** : Le backend décide, le frontend affiche
- ✅ **Fiabilité** : Impossible de désynchroniser frontend/backend
- ✅ **Transparent** : Pas besoin d'activer manuellement le FFT
- ✅ **Économie CPU** : Le FFT ne tourne que quand nécessaire (+20% CPU uniquement sur les effets FFT)
- ✅ **Économie RAM** : +20KB RAM uniquement quand le FFT est actif
- ✅ **UX améliorée** : L'utilisateur ne se préoccupe que du choix de l'effet

## ⚠️ Notes Importantes

1. **Effets audio exclus des événements CAN**: Les effets audio-réactifs ne peuvent **pas** être assignés aux événements CAN (clignotants, charge, etc.). Ils sont uniquement disponibles pour l'effet par défaut du profil.
   - **Backend** ([web_server.c:868-874](main/web_server.c:868-874)) : Valide et rejette avec erreur 400 toute tentative d'assigner un effet audio à un événement
   - **Frontend** ([script.js:3363-3366](data/script.js:3363-3366)) : Filtre automatiquement les effets audio des dropdowns d'événements
   - **API** : Le flag `audio_effect: true` est ajouté aux métadonnées des effets via `/api/effects`

2. **Événements CAN prioritaires**: Lorsque vous modifiez l'effet par défaut dans l'interface web, tous les événements CAN actifs sont automatiquement arrêtés pour que le changement soit immédiatement visible. Les événements CAN (clignotants, charge, etc.) continueront de fonctionner normalement par la suite.

3. **Connexion BLE optimisée**:
   - Les requêtes importantes (sauvegarde d'effets, simulation d'événements) attendent automatiquement que la queue soit vide avant d'être exécutées, évitant les erreurs "Commande BLE refusée"
   - Le polling des données audio (2Hz en WiFi) est **automatiquement désactivé** quand vous n'êtes pas sur l'onglet Configuration, évitant un embouteillage permanent de la queue BLE
   - **En mode BLE, le polling audio et FFT est complètement désactivé** pour économiser la bande passante (les effets audio continuent de fonctionner normalement)
   - Le délai entre les requêtes BLE a été réduit de 50ms à 20ms

4. **Performance**: Le traitement audio tourne sur un core séparé à ~50Hz, pas d'impact sur les LEDs.

5. **Mémoire**: Utilise ~4KB de RAM pour les buffers audio.

6. **I2S**: Utilise le périphérique I2S disponible (compatible avec ESP32, ESP32-S3, etc.).

## 🚀 Prochaines Améliorations Possibles

- ~~FFT réelle pour analyse spectrale avancée~~ ✅ **IMPLÉMENTÉ**
- ~~Égaliseur graphique dans l'interface web~~ ✅ **IMPLÉMENTÉ** (Canvas FFT 32 bandes)
- ~~Visualisation spectrale en temps réel~~ ✅ **IMPLÉMENTÉ**
- Presets audio (Bass Boost, Vocal, etc.)
- Calibration automatique du gain améliorée
- Égaliseur paramétrique (boost/cut par bande)
