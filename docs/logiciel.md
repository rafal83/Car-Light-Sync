# Car Light Sync — Logiciel

Ce document couvre les prérequis, l’installation et l’usage de base côté logiciel (sans détailler l’API JSON).

## Prérequis logiciels
- **PlatformIO** (recommandé) ou **ESP-IDF v5.0+**
- **Python 3.7+**
- Outils Git pour cloner le dépôt

## Compilation & flash (PlatformIO)
```bash
# Cloner
git clone https://github.com/raphaelgiga/car-light-sync.git
cd car-light-sync

# Compiler et flasher (profil ESP32-C6)
pio run -e esp32c6 -t upload
pio device monitor
```

## Compilation & flash (ESP-IDF)
```bash
# Installer ESP-IDF si besoin
git clone --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf
cd ~/esp/esp-idf && ./install.sh esp32c6 && . ./export.sh

# Cloner et construire le projet
git clone https://github.com/raphaelgiga/car-light-sync.git ~/projects/car-light-sync
cd ~/projects/car-light-sync
idf.py build
idf.py -p <port> flash monitor
```

## Configuration initiale
1. **Matériel** : éditer `include/config.h` (`LED_PIN`, `NUM_LEDS`, pins CAN…).
2. **WiFi (optionnel)** : renseigner `include/wifi_credentials.h`.
3. **Pins CAN** : ajuster dans `main/can_bus.c` si nécessaire.

## Interface web
- Point d’accès par défaut : `CarLightSync` (sans mot de passe), URL `http://192.168.4.1`.
- Pilotage en temps réel : effet, couleur, luminosité, vitesse, profils et événements CAN.
- OTA intégrée : upload du firmware directement depuis l’interface.

## Application mobile
- App iOS/Android (Capacitor) qui réutilise `data/index.html`, `data/script.js`, `data/style.css`.
- Connexion auto en BLE pour piloter le module sans WiFi.

## Mises à jour OTA
Depuis l’interface web : téléverser le binaire compilé (`.bin`) via la section OTA.
