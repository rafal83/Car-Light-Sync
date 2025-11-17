# Tesla Strip Controller

Système de contrôle LED RGB WS2812 pour Tesla, similaire au S3XY Strip, avec connexion au Commander (protocole Panda).

## 🚀 Caractéristiques

- **Contrôle LED WS2812** : Support de rubans LED RGB addressables
- **Protocole Panda** : Communication avec le Commander S3XY_OBD pour lire les données CAN de la Tesla
- **Interface Web** : Interface utilisateur moderne et responsive
- **16 Effets LED** : Rainbow, breathing, fire, strobe, animations Tesla, etc.
- **🆕 Système de Profils** : Jusqu'à 10 profils de configuration personnalisés
- **🆕 Association Événements CAN** : Effets LED déclenchés par les messages CAN (clignotants, charge, blindspot, etc.)
- **🆕 Mode Nuit Automatique** : Réduction automatique de luminosité basée sur capteur
- **🆕 Blindspot Detection** : Alertes visuelles pour détection angle mort
- **Synchronisation Véhicule** : Les LEDs réagissent à l'état du véhicule (portes, vitesse, charge, etc.)
- **WiFi Dual Mode** : Point d'accès pour configuration + client pour connexion au Commander
- **Sauvegarde Multiple** : Profils sauvegardés en mémoire non-volatile (NVS)
- **API REST Complète** : Contrôle programmatique via HTTP

## 📋 Prérequis

### Matériel
- ESP32 DevKit / ESP32-S2 Saola / ESP32-S3 DevKitC (ou compatible)
- Ruban LED WS2812 (ou WS2812B)
- Alimentation 5V appropriée pour les LEDs
- Tesla avec Commander Panda

### Logiciel
- ESP-IDF v5.0 ou supérieur
- PlatformIO (optionnel, mais recommandé)
- Python 3.7+

## 🔧 Installation

### Option 1: PlatformIO (Recommandé)

1. Cloner le repository :
```bash
git clone <repo-url>
cd tesla-strip
```

2. Ouvrir le projet dans PlatformIO

3. Configurer le fichier `include/config.h` selon votre matériel

4. Compiler et uploader :
```bash
pio run -t upload
pio device monitor
```

### Option 2: ESP-IDF

1. Installer ESP-IDF :
```bash
mkdir -p ~/esp
cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32
. ./export.sh
```

2. Cloner le projet :
```bash
git clone <repo-url>
cd tesla-strip
```

3. Configurer et compiler :
```bash
idf.py menuconfig  # Optionnel
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## ⚙️ Configuration

### Configuration GPIO

Éditer `include/config.h` :

```c
#define LED_PIN             5        // Pin GPIO pour les LEDs
#define NUM_LEDS            60       // Nombre de LEDs
#define LED_TYPE            WS2812B  // Type de LED
```

### Configuration WiFi

```c
#define WIFI_AP_SSID        "Tesla-Strip"      // SSID du point d'accès
#define WIFI_AP_PASSWORD    "tesla123"         // Mot de passe
#define PANDA_WIFI_SSID     "panda-"          // Préfixe SSID du Commander
#define PANDA_WIFI_PASSWORD "testing123"       // Mot de passe Commander
```

### Configuration Commander

```c
#define COMMANDER_PORT      1338             // Port du Commander
#define PANDA_WIFI_SSID     "S3XY_OBD"      // SSID du Commander
#define PANDA_WIFI_PASSWORD "12345678"       // Mot de passe
#define COMMANDER_IP        "192.168.4.1"   // IP fixe du Commander
```

## 🎨 Effets LED Disponibles

Le système propose **19 effets LED** identifiés par des ID alphanumériques :

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

## 🚗 Messages CAN Supportés (Tesla Model 3 2021)

Le système décode les messages CAN suivants :

| ID    | Description              | Données extraites                    |
|-------|--------------------------|--------------------------------------|
| 0x118 | État véhicule            | Contact, position vitesse (P/R/N/D) |
| 0x257 | Vitesse                  | Vitesse en km/h                      |
| 0x2B4 | État des portes          | 4 portes (ouvert/fermé)             |
| 0x2B5 | Verrouillage            | État verrouillé/déverrouillé        |
| 0x2C4 | État des fenêtres        | Position des 4 fenêtres (0-100%)    |
| 0x2E5 | Coffre/Frunk            | État coffre et frunk                |
| 0x3E5 | Lumières                | Phares, feux de route, brouillard   |
| 0x2C3 | Freins                  | État pédale de frein                |
| 0x3F5 | Clignotants             | Gauche/Droite/Warning               |
| 0x3D2 | État de charge          | État, %, puissance                  |
| 0x392 | Tension batterie 12V    | Voltage                             |
| 0x2A5 | **Blindspot**           | **Détection angle mort L/R**        |
| 0x3C8 | **Mode Nuit**           | **État capteur luminosité**         |
| 0x118 | **Autopilot & Vitesses**| **Autopilot, P/R/N/D**             |

## 🌐 Interface Web

### Accès à l'interface

1. Connectez-vous au WiFi `Tesla-Strip` (mot de passe: `tesla123`)
2. Ouvrez un navigateur à l'adresse : `http://192.168.4.1`

### Fonctionnalités

- **Contrôle des effets** : Sélection de l'effet, luminosité, vitesse, couleurs
- **🆕 Gestion des Profils** : Création, activation, suppression de profils
- **🆕 Association Événements** : Assigner des effets spécifiques aux événements CAN
- **🆕 Mode Nuit Auto** : Configuration du mode nuit automatique
- **Connexion Commander** : Connexion automatique au S3XY_OBD
- **État du véhicule** : Affichage en temps réel des données CAN (incluant blindspot et mode nuit)
- **Sauvegarde** : Persistance des profils et configurations

### API REST

L'interface expose une API REST :

#### GET `/api/status`
Retourne l'état du système (WiFi, Commander, véhicule)

#### GET `/api/config`
Retourne la configuration actuelle des LEDs

#### POST `/api/effect`
Configure un nouvel effet
```json
{
  "effect": 3,
  "brightness": 128,
  "speed": 50,
  "color1": 16711680,
  "color2": 65280,
  "color3": 255
}
```

#### POST `/api/save`
Sauvegarde la configuration

#### POST `/api/commander/connect`
Recherche et connexion au Commander

#### POST `/api/commander/disconnect`
Déconnexion du Commander

#### 🆕 GET `/api/profiles`
Liste tous les profils disponibles

#### 🆕 POST `/api/profile/activate`
Active un profil
```json
{"profile_id": 1}
```

#### 🆕 POST `/api/profile/create`
Crée un nouveau profil
```json
{"name": "Mon Profil"}
```

#### 🆕 POST `/api/profile/delete`
Supprime un profil
```json
{"profile_id": 2}
```

#### 🆕 GET `/api/effects`
Liste tous les effets disponibles avec leurs IDs et noms

#### 🆕 GET `/api/event-types`
Liste tous les types d'événements CAN disponibles

#### 🆕 GET `/api/events`
Obtient la configuration de tous les événements du profil actif

#### 🆕 POST `/api/events`
Met à jour la configuration des événements
```json
{
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
    }
  ]
}
```

#### 🆕 POST `/api/profile/update/default`
Met à jour l'effet par défaut d'un profil
```json
{
  "profile_id": 0,
  "effect": 2,
  "brightness": 150,
  "speed": 80,
  "color1": 16711680
}
```

## 🔌 Protocole Panda

Le protocole Panda est utilisé pour communiquer avec le Commander :

### Structure d'un message

```
[Type][Bus][Length_H][Length_L][CAN_ID][DLC][Data...]
```

- **Type** : Type de message (1=CAN_RECV, 2=CAN_SEND, 3=HEARTBEAT)
- **Bus** : Bus CAN (0=Chassis, 1=Powertrain, 2=Body)
- **Length** : Longueur des données (big-endian)
- **CAN_ID** : Identifiant CAN (32 bits)
- **DLC** : Data Length Code (0-8)
- **Data** : Données CAN (0-8 bytes)

### Exemple de connexion

1. Connexion TCP au Commander sur le port 1338
2. Envoi périodique de heartbeats (toutes les secondes)
3. Réception des frames CAN du véhicule
4. Décodage et mise à jour de l'état du véhicule

## 📊 Architecture du Code

```
tesla-strip/
├── include/
│   ├── config.h              # Configuration principale
│   ├── wifi_manager.h        # Gestion WiFi
│   ├── commander.h           # Communication Commander
│   ├── tesla_can.h           # Décodage CAN Tesla
│   ├── led_effects.h         # Effets LED
│   └── web_server.h          # Serveur web
├── main/
│   ├── main.c                # Programme principal
│   ├── wifi_manager.c        # Implémentation WiFi
│   ├── commander.c           # Implémentation Commander
│   ├── tesla_can.c           # Implémentation décodage CAN
│   ├── led_effects.c         # Implémentation effets LED
│   └── web_server.c          # Implémentation serveur web
├── data/
│   └── index.html            # Interface web
├── CMakeLists.txt            # Configuration CMake
├── platformio.ini            # Configuration PlatformIO
├── partitions.csv            # Table de partitions
└── README.md                 # Ce fichier
```

## 🐛 Débogage

### Moniteur série

```bash
# PlatformIO
pio device monitor

# ESP-IDF
idf.py monitor
```

### Niveaux de log

Éditer `sdkconfig.defaults` pour changer le niveau de log :
```
CONFIG_LOG_DEFAULT_LEVEL_DEBUG=y
```

### Commandes utiles

```bash
# Effacer la mémoire flash
idf.py erase-flash

# Moniteur série avec filtre
idf.py monitor --print-filter="WiFi:I LED:D"
```

## 🔒 Sécurité

⚠️ **Important** :
- Changez les mots de passe par défaut dans `config.h`
- Le système n'utilise pas de chiffrement par défaut
- Ne connectez pas le système à un réseau non sécurisé

## 📝 Licence

Ce projet est sous licence MIT. Voir le fichier LICENSE pour plus de détails.

## 🤝 Contribution

Les contributions sont les bienvenues ! N'hésitez pas à :
- Ouvrir une issue pour signaler un bug
- Proposer de nouvelles fonctionnalités
- Soumettre des pull requests

## 📚 Références

- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/)
- [WS2812 Datasheet](https://cdn-shop.adafruit.com/datasheets/WS2812.pdf)
- [Tesla CAN Bus Reverse Engineering](https://github.com/joshwardell/model3dbc)
- [Comma.ai Panda](https://github.com/commaai/panda)

## ⚡ Performances

- **Fréquence LED** : 50 FPS (20ms)
- **Latence CAN** : < 100ms
- **Consommation RAM** : 14.5% (47KB / 320KB)
- **Consommation Flash** : 49.6% (975KB / 1966KB)
- **Clients WiFi simultanés** : 4 maximum
- **Stack HTTP** : 16KB (optimisé pour profils)
- **Timeout HTTP** : 30s (réception/envoi)

## 🎯 Roadmap

- [x] ~~Système de profils multiples~~ ✅ v2.0
- [x] ~~Association événements CAN → Effets~~ ✅ v2.0
- [x] ~~Mode nuit automatique~~ ✅ v2.0
- [x] ~~Import/Export de profils~~ ✅ v2.1
- [x] ~~OTA Updates~~ ✅ v2.1
- [x] ~~Optimisation mémoire HTTP~~ ✅ v2.1
- [ ] Support de plusieurs rubans LED
- [ ] Intégration HomeAssistant/MQTT
- [ ] Mode musique avec micro I2S
- [ ] Support BLE pour la configuration
- [ ] Application mobile iOS/Android
- [ ] Synchronisation multi-véhicules
- [ ] Enregistrement d'effets personnalisés

## 💡 Exemples d'utilisation

### Animation à l'ouverture des portes
```c
if (vehicle_state.door_fl || vehicle_state.door_fr) {
    led_effects_set_config(&door_open_effect);
}
```

### Alerte batterie faible
```c
if (vehicle_state.battery_voltage < 11.5) {
    led_effects_set_solid_color(0xFF0000); // Rouge
}
```

### Indicateur de charge complet
```c
if (vehicle_state.charging && vehicle_state.charge_percent >= 80) {
    led_effects_set_solid_color(0x00FF00); // Vert
}
```

## 🔧 Dépannage

### Problème : LEDs ne s'allument pas
- Vérifier la connexion GPIO5
- Vérifier l'alimentation 5V des LEDs
- Vérifier la masse commune ESP32 ↔ LEDs
- Vérifier `LED_PIN` et `NUM_LEDS` dans config.h
- Tester avec un effet simple (Solid)

### Problème : Pas de connexion au Commander
- Vérifier que le Commander est allumé et en WiFi
- SSID attendu : "S3XY_OBD" (configurable dans config.h)
- Mot de passe : "12345678" (configurable)
- IP fixe : 192.168.4.1:1338
- Vérifier les logs série pour erreurs de connexion
- Tester ping vers 192.168.4.1 après connexion WiFi

### Problème : Interface web inaccessible
- Vérifier connexion au WiFi "Tesla-Strip"
- Essayer http://192.168.4.1 (PAS https)
- Vider le cache du navigateur (Ctrl+F5)
- Vérifier dans les logs série : "Page HTML envoyée avec succès"
- Si erreur "ESP_ERR_HTTPD_RESP_SEND", redémarrer l'ESP32

### Problème : Guru Meditation Error / Stack Overflow
- ✅ **Résolu en v2.1.0** grâce à l'allocation dynamique
- Si le problème persiste, vérifier version du firmware
- Reflasher avec `pio run -t upload`

### Problème : Profils ne se chargent pas
- Vérifier compatibilité version (v2.1+ requis)
- Les anciens profils (<v2.1) sont automatiquement ignorés
- Faire un factory reset si nécessaire : POST `/api/factory-reset`
- Créer de nouveaux profils via l'interface web

---

**Développé avec ❤️ pour la communauté Tesla**
