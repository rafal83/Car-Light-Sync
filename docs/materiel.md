# Car Light Sync — Matériel

Document de référence pour tout ce qui touche au hardware : composants, câblage et points d'attention avant d'alimenter le système.

## Matériel requis
- **ESP32-C6 DevKitC (recommandé et nécessaire)** : seul l'ESP32-C6 offre **2 interfaces TWAI** et, avec ESP-IDF ≥ 5.2 (support multi-contrôleurs), permet d'activer toutes les fonctionnalités CAN (BODY + CHASSIS).
- **ESP32-S3 (option de secours)** : fonctionne avec **1 seul bus CAN** → fonctionnalités limitées (pas de double bus). Le second bus est automatiquement désactivé dans le code.
- **Ruban LED WS2812/WS2812B** : 60-150 LEDs recommandées.
  - ⚠️ **Attention câblage** : certains rubans inversent rouge/noir (rouge = GND, noir = +5V). Vérifier avant d'alimenter.
  - 🔧 **Tester d'abord** avec 3.3V pour valider la polarité.
- **Transceiver CAN** : SN65HVD230, MCP2551 ou équivalent 3.3V.
- **Connecteur CAN véhicule** : câble porte/pilier A (Tesla) ou OBD/20-pin selon modèle.
- **Alimentation 5V** : 3–10A selon la longueur du ruban.
- **Micro INMP441 (optionnel)** : pour le mode audio-réactif.
- **ESP-NOW satellites (optionnel)** : modules ESP32-C6 configurés en esclave (profils PlatformIO `esp32c6_bll`, `esp32c6_blr`, `esp32c6_speedometer`) pour déporter des fonctions blindspot ou compteur de vitesse.

## Câblage LED
- Par défaut `LED_PIN = 5` et `NUM_LEDS = 112` (à adapter dans `include/config.h`).
- Utiliser du fil 18–22 AWG pour l'alim +5V et GND.
- Ajouter un condensateur 1000 µF (5–16V) entre +5V/GND côté ruban et une résistance série de 330–470 Ω sur la ligne data.

## Connexion CAN
- GPIO par défaut : `CONFIG_CAN_TX_GPIO = 8`, `CONFIG_CAN_RX_GPIO = 7` (configurable dans `main/can_bus.c`).
- Transceiver typique :
  - ESP32 TX → TX du transceiver
  - ESP32 RX → RX du transceiver
  - 3V3 → VCC transceiver, GND commun
  - CAN_H/CAN_L → bus CAN du véhicule (connexion en parallèle, non invasive)
- Vitesse par défaut : 500 kbit/s (adapter selon le véhicule si besoin).

## LED indicateur et bouton reset
- LED statut intégrée :
  - ESP32-S3 : GPIO 21
  - ESP32-C6 : GPIO 8
- Bouton reset (GPIO 4) :
  - Appui 5s = **factory reset** (efface NVS, profils, WiFi).

## Emplacements CAN utiles (exemples Tesla)
1. Port OBD-II (6 = CAN_H, 14 = CAN_L, 4/5 = GND)
2. Connecteur derrière le centre média (Model 3/Y)
3. Connecteur sous siège conducteur (Model S/X)

**Important** : toujours vérifier la polarité et la continuité avant de brancher l'alimentation principale.
