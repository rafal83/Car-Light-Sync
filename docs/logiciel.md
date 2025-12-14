# Car Light Sync — Logiciel

Ce document couvre les prérequis, l'installation et l'usage de base côté logiciel (sans détailler l'API JSON).

## Prérequis logiciels
- **PlatformIO** (recommandé) ou **ESP-IDF v5.2+** (v5.2 requis pour le support multi-contrôleur TWAI)
- **Python 3.7+**
- Outils Git pour cloner le dépôt

## Compilation & flash (PlatformIO)
```bash
# Cloner
git clone https://github.com/raphaelgiga/car-light-sync.git
cd car-light-sync

# Compiler et flasher (profil ESP32-C6, seul profil offrant 2 TWAI et toutes les fonctionnalités CAN)
pio run -e esp32c6 -t upload
pio device monitor
```
> ESP32-S3 : possible avec `-e esp32s3` ou `-e esp32s3_n4r2`, mais limité à 1 bus CAN (pas toutes les fonctionnalités).
> Profils ESP-NOW satellites : `-e esp32c6_bll`, `-e esp32c6_blr`, `-e esp32c6_speedometer` (côté esclave). Sans ces profils, le firmware reste maître ESP-NOW par défaut.

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
> Remarque : le double bus CAN (BODY + CHASSIS) nécessite ESP-IDF ≥ 5.2 et une cible disposant de 2 contrôleurs TWAI (ESP32-C6). Sinon, le second bus est automatiquement désactivé côté firmware.

## Configuration initiale
1. **Matériel** : éditer `include/config.h` (`LED_PIN`, `NUM_LEDS`, pins CAN…).
2. **WiFi (optionnel)** : renseigner `include/wifi_credentials.h`.
3. **Pins CAN** : ajuster dans `main/can_bus.c` si nécessaire.

## Interface web
- Point d'accès par défaut : `CarLightSync` (sans mot de passe), URL `http://192.168.4.1`.
- Pilotage en temps réel : effet, couleur, luminosité, vitesse, profils et événements CAN.
- OTA intégrée : upload du firmware directement depuis l'interface.
- Contrôle des services : démarrer/arrêter GVRET TCP, CANServer UDP, gérer l'autostart.
- Télémetrie système : CPU, mémoire, statut CAN, stockage NVS.

## Application mobile
- App iOS/Android (Capacitor) qui réutilise `data/index.html`, `data/script.js`, `data/style.css`.
- Connexion auto en BLE pour piloter le module sans WiFi.

## Mises à jour OTA
Depuis l'interface web : téléverser le binaire compilé (`.bin`) via la section OTA.
