# Car Light Sync

Système de contrôle LED RGB WS2812 avec connexion CAN Bus directe, intégration CAN unifiée et interface web moderne. Compatible Tesla et autres véhicules.

## 🚀 Caractéristiques Principales

### Système LED Avancé
- **Support WS2812/WS2812B** : Rubans LED RGB addressables haute performance
- **19 Effets LED Intégrés** : Rainbow, breathing, fire, strobe, animations véhicule, blindspot flash, etc.
- **Système de Profils** : Jusqu'à 10 profils de configuration personnalisés sauvegardés en NVS
- **Mode Nuit Automatique** : Réduction automatique de luminosité basée sur capteur véhicule
- **Performances** : 50 FPS (20ms par frame), latence CAN < 100ms

### Intégration CAN Unifiée
- **Architecture Modulaire** : Système CAN unifié basé sur DBC avec décodage générique
- **Support Multi-Véhicules** : Configuration par fichier pour différents véhicules (Tesla Model 3, Y, S, etc.)
- **22+ Événements CAN** : Détection intelligente des événements véhicule (clignotants, portes, charge, blindspot, autopilot, etc.)
- **Mapping Signal → État** : Mapping automatique des signaux CAN vers l'état du véhicule
- **Gestion d'Événements** : Support des conditions RISING_EDGE, FALLING_EDGE, VALUE_EQUALS, THRESHOLD, etc.

### Connectivité & Interface
- **WiFi Dual Mode** : Point d'accès (configuration) + Client (connexion réseau)
- **Interface Web Moderne** : Interface responsive avec gestion complète des profils et événements
- **API REST Complète** : Contrôle programmatique via HTTP avec 30+ endpoints
- **OTA Updates** : Mise à jour firmware over-the-air via interface web
- **Support BLE** : API BLE pour configuration mobile (optionnel)

### Fonctionnalités Avancées
- **Association Événements CAN → Effets** : Chaque événement déclenche un effet LED personnalisé
- **Système de Priorité** : Gestion intelligente des effets simultanés (0-255)
- **Effets Temporaires** : Durée configurable avec retour automatique à l'effet par défaut
- **Blindspot Detection** : Alertes visuelles pour détection angle mort (priorité maximale)
- **Synchronisation Véhicule** : Les LEDs réagissent en temps réel à l'état du véhicule

## 📋 Prérequis

### Matériel
- **ESP32** : ESP32-DevKit, ESP32-S2-Saola, ou ESP32-S3-DevKitC (support PSRAM)
- **Ruban LED** : WS2812 ou WS2812B (60-150 LEDs recommandé)
- **Alimentation** : 5V 3-10A selon nombre de LEDs
- **Module CAN** : Transceiver CAN (ex: SN65HVD230, MCP2551) connecté au bus CAN du véhicule
- **Véhicule** : Véhicule compatible avec bus CAN (Tesla Model 3, Y, S, X, ou autres)

### Logiciel
- **ESP-IDF** : v5.0 ou supérieur
- **PlatformIO** : Recommandé pour compilation et flash
- **Python 3.7+** : Pour scripts de build

## 🔧 Installation

### Méthode 1 : PlatformIO (Recommandé)

```bash
# Cloner le repository
git clone <repo-url>
cd car-light-sync

# Sélectionner l'environnement selon votre ESP32
# esp32dev (ESP32 standard)
# esp32s2 (ESP32-S2 avec PSRAM)
# esp32s3 (ESP32-S3 avec PSRAM)

# Compiler et uploader
pio run -e esp32s3 -t upload
pio device monitor
```

### Méthode 2 : ESP-IDF

```bash
# Installer ESP-IDF (si pas déjà fait)
mkdir -p ~/esp
cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32,esp32s2,esp32s3
. ./export.sh

# Cloner et compiler le projet
cd ~/projects
git clone <repo-url>
cd car-light-sync

# Configurer (optionnel)
idf.py menuconfig

# Compiler et flash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### Configuration Initiale

1. **Configurer le matériel** dans [include/config.h](include/config.h) :
```c
#define LED_PIN             5        // GPIO pour signal LED
#define NUM_LEDS            94       // Nombre de LEDs sur le ruban
#define LED_TYPE            WS2812B
#define COLOR_ORDER         GRB
```

2. **Configurer le WiFi** dans [include/wifi_credentials.h](include/wifi_credentials.h) (optionnel)

3. **Configurer les GPIO CAN** dans [main/can_bus.c](main/can_bus.c) :
```c
#define CONFIG_CAN_TX_GPIO  GPIO_NUM_38  // TX du transceiver CAN
#define CONFIG_CAN_RX_GPIO  GPIO_NUM_39  // RX du transceiver CAN
```

## ⚙️ Configuration CAN Multi-Véhicules

Le système utilise une architecture CAN unifiée permettant de supporter plusieurs véhicules via des fichiers de configuration.

### Fichiers de Configuration Générés

Le projet génère automatiquement les fichiers de configuration CAN :
- [main/vehicle_can_unified_config.generated.c](main/vehicle_can_unified_config.generated.c)
- [include/vehicle_can_unified_config.generated.h](include/vehicle_can_unified_config.generated.h)

Ces fichiers sont générés à partir de la définition DBC et contiennent :
- Définitions des messages CAN (ID, DLC, signaux)
- Définitions des signaux (start_bit, length, byte_order, factor, offset)
- Mapping des événements CAN

### Architecture CAN

```
vehicle_can_unified.c          → Pipeline de traitement CAN unifié
vehicle_can_mapping.c          → Mapping signal → vehicle_state
vehicle_can_unified_config.generated.c → Définitions messages/signaux (auto-généré)
```

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

## 🚗 Événements CAN Supportés

Le système détecte 22+ événements CAN du véhicule Tesla :

| Événement | Déclencheur | Priorité Suggérée |
|-----------|-------------|-------------------|
| `TURN_LEFT` | Clignotant gauche actif | 200 |
| `TURN_RIGHT` | Clignotant droit actif | 200 |
| `TURN_HAZARD` | Warning activé | 220 |
| `CHARGING` | Début de charge | 150 |
| `CHARGE_COMPLETE` | Charge ≥ 80% terminée | 140 |
| `DOOR_OPEN` | Ouverture d'une porte | 100 |
| `DOOR_CLOSE` | Fermeture portes | 90 |
| `LOCKED` | Véhicule verrouillé | 110 |
| `UNLOCKED` | Véhicule déverrouillé | 110 |
| `BRAKE_ON` | Frein appuyé | 180 |
| `BRAKE_OFF` | Frein relâché | 170 |
| `BLINDSPOT_LEFT` | Angle mort gauche détecté | 250 |
| `BLINDSPOT_RIGHT` | Angle mort droit détecté | 250 |
| `NIGHT_MODE_ON` | Mode nuit activé | 0 (auto) |
| `NIGHT_MODE_OFF` | Mode nuit désactivé | 0 (auto) |
| `AUTOPILOT_ENGAGED` | Autopilot activé | 120 |
| `AUTOPILOT_DISENGAGED` | Autopilot désactivé | 120 |
| `GEAR_DRIVE` | Passage en mode Drive (D) | 80 |
| `GEAR_REVERSE` | Passage en marche arrière (R) | 80 |
| `GEAR_PARK` | Passage en mode Park (P) | 80 |
| `SPEED_THRESHOLD` | Vitesse > seuil configurable | 60 |

## 🌐 Interface Web

### Accès à l'Interface

1. Se connecter au WiFi **CarLightSync** (sans mot de passe)
2. Ouvrir un navigateur à l'adresse : `http://192.168.10.1`

### Fonctionnalités de l'Interface

- **Contrôle en Temps Réel** : Sélection effet, luminosité, vitesse, couleurs
- **Gestion des Profils** : Création, activation, suppression de profils (max 10)
- **Association Événements CAN** : Assigner des effets spécifiques aux événements CAN
- **Mode Nuit Automatique** : Configuration du mode nuit avec luminosité réduite
- **État du Véhicule** : Affichage en temps réel des données CAN (vitesse, charge, portes, blindspot, etc.)
- **Connexion CAN** : Connexion directe au bus CAN du véhicule via transceiver
- **OTA Updates** : Mise à jour firmware via upload de fichier

### API REST

L'interface expose une API REST complète. Voir section [API REST](#-api-rest) ci-dessous.

## 🔌 Connexion CAN Directe

### Configuration du Module CAN

Le système utilise le driver TWAI (Two-Wire Automotive Interface) de l'ESP32 pour une connexion directe au bus CAN :

- **Driver** : ESP-IDF TWAI (driver CAN intégré ESP32)
- **Vitesse** : 500 kbit/s (configurable selon véhicule)
- **GPIO TX** : GPIO 38 (configurable)
- **GPIO RX** : GPIO 39 (configurable)
- **Mode** : Normal (réception + transmission)
- **Transceiver** : SN65HVD230, MCP2551 ou compatible 3.3V

### Câblage du Transceiver CAN

```
ESP32                    Transceiver CAN            Bus CAN Véhicule
┌─────────────┐         ┌─────────────┐           ┌──────────────┐
│             │         │             │           │              │
│  GPIO 38 TX │────────►│ TX          │           │              │
│             │         │             │           │              │
│  GPIO 39 RX │◄────────│ RX      CAN_H├──────────┤ CAN_H        │
│             │         │         CAN_L├──────────┤ CAN_L        │
│         3V3 │────────►│ VCC         │           │              │
│         GND │────────►│ GND         │           │ GND          │
│             │         │             │           │              │
└─────────────┘         └─────────────┘           └──────────────┘
```

### Accès au Bus CAN du Véhicule

**Emplacements d'accès au bus CAN (exemple pour Tesla) :**

1. **Port OBD-II** (sous le volant) :
   - Pin 6 : CAN_H
   - Pin 14 : CAN_L
   - Pin 4/5 : GND

2. **Connecteur derrière le  centre média** (Model 3/Y)

3. **Connecteur sous le siège conducteur** (Model S/X)

⚠️ **Important** : Connexion en parallèle (non invasive), ne pas interrompre le bus CAN existant.

## 📊 Architecture du Code

```
car-light-sync/
├── include/                              # Headers
│   ├── config.h                          # Configuration matérielle
│   ├── vehicle_can_unified.h             # API CAN unifiée
│   ├── vehicle_can_unified_config.h      # Structures CAN
│   ├── vehicle_can_unified_config.generated.h  # Config auto-générée
│   ├── vehicle_can_mapping.h             # Mapping signal → état
│   ├── led_effects.h                     # Effets LED
│   ├── web_server.h                      # Serveur web
│   ├── wifi_manager.h                    # Gestion WiFi
│   ├── config_manager.h                  # Gestion profils
│   ├── can_bus.h                         # Bus CAN (TWAI driver)
│   ├── ota_update.h                      # Mises à jour OTA
│   └── ble_api_service.h                 # API BLE (optionnel)
├── main/                                 # Sources
│   ├── main.c                            # Programme principal
│   ├── vehicle_can_unified.c             # Pipeline CAN unifié
│   ├── vehicle_can_unified_config.generated.c  # Config CAN auto-générée
│   ├── vehicle_can_mapping.c             # Implémentation mapping
│   ├── led_effects.c                     # Implémentation effets LED
│   ├── web_server.c                      # Implémentation serveur web
│   ├── wifi_manager.c                    # Implémentation WiFi
│   ├── config_manager.c                  # Implémentation profils
│   ├── can_bus.c                         # Implémentation bus CAN
│   ├── ota_update.c                      # Implémentation OTA
│   └── ble_api_service.c                 # Implémentation BLE
├── data/                                 # Ressources web
│   ├── index.html                        # Interface web (compressée)
│   └── icon.svg                          # Icône
├── tools/                                # Scripts utilitaires
├── docs/                                 # Documentation
├── CMakeLists.txt                        # Configuration CMake
├── platformio.ini                        # Configuration PlatformIO
├── partitions.csv                        # Table de partitions
├── sdkconfig.esp32dev                    # Config ESP32 standard
├── sdkconfig.esp32s2                     # Config ESP32-S2
├── sdkconfig.esp32s3                     # Config ESP32-S3
└── README.md                             # Ce fichier
```

## 🎯 API REST

### Statut et Configuration

```bash
# Obtenir l'état du système
GET /api/status

# Obtenir la configuration actuelle
GET /api/config
```

### Contrôle des Effets

```bash
# Changer l'effet LED
POST /api/effect
Content-Type: application/json
{
  "effect": "RAINBOW",
  "brightness": 150,
  "speed": 80,
  "color1": 16711680,  # RGB en décimal (0xFF0000 = rouge)
  "color2": 65280,
  "color3": 255
}

# Sauvegarder la configuration
POST /api/save
```

### Gestion des Profils

```bash
# Lister tous les profils
GET /api/profiles

# Créer un nouveau profil
POST /api/profile/create
{"name": "Mon Profil Sport"}

# Activer un profil
POST /api/profile/activate
{"profile_id": 1}

# Supprimer un profil
POST /api/profile/delete
{"profile_id": 2}

# Mettre à jour l'effet par défaut d'un profil
POST /api/profile/update/default
{
  "profile_id": 0,
  "effect": "BREATHING",
  "brightness": 150,
  "speed": 80,
  "color1": 16711680
}
```

### Gestion des Événements CAN

```bash
# Lister tous les effets disponibles
GET /api/effects

# Lister tous les types d'événements CAN
GET /api/event-types

# Obtenir la configuration de tous les événements
GET /api/events

# Mettre à jour la configuration des événements
POST /api/events
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
```

### CAN Bus & OTA

```bash
# Obtenir le statut du bus CAN
GET /api/can/status

# Mise à jour OTA (upload binaire)
POST /api/ota/update
Content-Type: multipart/form-data
```

## ⚡ Performances & Spécifications

### Performances Système
- **Fréquence LED** : 50 FPS (20ms par frame)
- **Latence CAN** : < 100ms du message CAN à l'affichage LED
- **Détection Événements** : 100ms entre chaque vérification
- **Consommation RAM** : 14.5% (47KB / 320KB)
- **Consommation Flash** : 49.6% (975KB / 1966KB)

### Configuration Réseau
- **Clients WiFi simultanés** : 4 maximum
- **Stack HTTP** : 16KB par connexion
- **Timeout HTTP** : 30s (réception/envoi)
- **Interface web compressée** : ~18KB gzip

### Limites
- **Profils maximum** : 10 profils sauvegardés
- **Événements CAN** : 22+ types d'événements
- **Effet temporaire max** : 60 secondes
- **Priorité** : 0-255
- **LEDs recommandé** : 60-150 LEDs (300+ possible avec injection de courant)

## 🔧 Dépannage

### Problème : LEDs ne s'allument pas
- Vérifier la connexion GPIO5 (ou pin configuré)
- Vérifier l'alimentation 5V des LEDs
- Vérifier la masse commune ESP32 ↔ LEDs
- Vérifier `LED_PIN` et `NUM_LEDS` dans [config.h](include/config.h)
- Tester avec un effet simple (Solid blanc)

### Problème : Pas de messages CAN reçus
- Vérifier le câblage du transceiver CAN (CAN_H, CAN_L, GND)
- Vérifier les GPIO TX (38) et RX (39) dans [can_bus.c](main/can_bus.c)
- Vérifier que le transceiver est alimenté en 3.3V
- Vérifier la résistance de terminaison (120Ω si nécessaire)
- Vérifier les logs série : "Bus CAN démarré" et "CAN frame received"
- Utiliser un outil de diagnostic CAN pour vérifier le bus

### Problème : Interface web inaccessible
- Vérifier connexion au WiFi `Car-Light-Sync`
- Essayer `http://192.168.10.1` (PAS https)
- Vider le cache du navigateur (Ctrl+F5)
- Vérifier logs série : "Page HTML envoyée avec succès"
- Si erreur persistante, redémarrer l'ESP32

### Problème : Profils ne se chargent pas
- Vérifier compatibilité version (v2.1+ requis)
- Factory reset si nécessaire : `POST /api/factory-reset`
- Créer de nouveaux profils via l'interface web

### Problème : Guru Meditation Error / Stack Overflow
- ✅ Résolu en v2.1.0 grâce à l'allocation dynamique
- Si le problème persiste, mettre à jour le firmware
- Reflasher avec `pio run -t upload`

## 🎓 Guides & Documentation

- **[QUICKSTART.md](QUICKSTART.md)** : Guide de démarrage rapide en 5 minutes
- **[TECHNICAL.md](TECHNICAL.md)** : Documentation technique approfondie (architecture CAN, mémoire, optimisations)
- **[WIRING.md](WIRING.md)** : Guide de câblage détaillé avec schémas
- **[CHANGELOG.md](CHANGELOG.md)** : Historique des versions et modifications

## 🎯 Roadmap

- [x] ~~Système de profils multiples~~ ✅ v2.0
- [x] ~~Association événements CAN → Effets~~ ✅ v2.0
- [x] ~~Mode nuit automatique~~ ✅ v2.0
- [x] ~~Import/Export de profils~~ ✅ v2.1
- [x] ~~OTA Updates~~ ✅ v2.1
- [x] ~~Optimisation mémoire HTTP~~ ✅ v2.1
- [x] ~~Architecture CAN unifiée~~ ✅ v2.2
- [x] ~~Support multi-véhicules~~ ✅ v2.2
- [ ] Support de plusieurs rubans LED (multi-GPIO)
- [ ] Intégration HomeAssistant/MQTT
- [ ] Mode musique avec micro I2S
- [ ] Support BLE pour configuration mobile
- [ ] Application mobile iOS/Android
- [ ] Synchronisation multi-véhicules
- [ ] Enregistrement d'effets personnalisés via interface web

## 🔒 Sécurité

### Avertissements Importants
- ⚠️ **Changez les mots de passe par défaut** dans [config.h](include/config.h) et [wifi_credentials.h](include/wifi_credentials.h)
- ⚠️ Le système n'utilise pas de chiffrement par défaut sur le WiFi AP
- ⚠️ Ne connectez pas le système à un réseau non sécurisé sans VPN
- ⚠️ L'accès à l'interface web n'est pas protégé par mot de passe

### Bonnes Pratiques
- Utiliser un mot de passe WiFi fort (min 12 caractères)
- Limiter l'accès physique à l'ESP32
- Désactiver l'AP WiFi quand non utilisé
- Surveiller les logs pour connexions suspectes

## 📝 Licence

Ce projet est sous licence MIT. Voir le fichier LICENSE pour plus de détails.

## 🤝 Contribution

Les contributions sont les bienvenues ! Pour contribuer :

1. Fork le projet
2. Créer une branche feature (`git checkout -b feature/AmazingFeature`)
3. Commit les changements (`git commit -m 'Add AmazingFeature'`)
4. Push vers la branche (`git push origin feature/AmazingFeature`)
5. Ouvrir une Pull Request

### Zones de Contribution Prioritaires
- Configurations CAN pour autres véhicules (Tesla, BMW, Audi, etc.)
- Nouveaux effets LED créatifs
- Optimisations de performance
- Documentation et traductions
- Tests et validation

## ☕ Soutenir le projet

Si ce projet vous est utile et que vous souhaitez soutenir son développement, vous pouvez m'offrir un café sur [Buy Me a Coffee](https://buymeacoffee.com/raphael.d). Merci pour votre aide !

## 📚 Références

- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/)
- [WS2812 Datasheet](https://cdn-shop.adafruit.com/datasheets/WS2812.pdf)
- [Tesla Model 3 CAN Bus DBC](https://github.com/joshwardell/model3dbc)
- [ESP32 TWAI (CAN) Driver](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/twai.html)
- [PlatformIO Documentation](https://docs.platformio.org/)

## 💡 Support & Communauté

- **Issues GitHub** : Pour signaler bugs et proposer fonctionnalités
- **Discussions** : Pour questions et partage d'expériences
- **Wiki** : Documentation communautaire et guides

---

**Développé avec ❤️ pour la communauté automobile**

Version actuelle : **v2.2.0** | Dernière mise à jour : 2025-11-20
