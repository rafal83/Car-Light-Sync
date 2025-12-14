# Car Light Sync

Système de contrôle LED RGB WS2812 synchronisé au bus CAN, avec interface web/mobile et mises à jour OTA. Projet open source, non lucratif et orienté communauté.

## ☕ Soutenir le projet
Car Light Sync est maintenu sur mon temps libre. Tu peux aider en :
- Mettre une étoile au dépôt et partager le projet
- Contribuer au code, à la doc ou aux tests (issues/PR bienvenues)
- Offrir un café pour financer matériel, hébergement et prototypes : [![Buy Me A Coffee](https://www.buymeacoffee.com/assets/img/custom_images/orange_img.png)](https://buymeacoffee.com/raphael.d)

Merci ! Ton soutien garde le projet libre et accessible.

## 🚀 Aperçu rapide
- LEDs WS2812/WS2812B avec effets audio-réactifs
- Intégration CAN multi-véhicules (architecture unifiée, double TWAI requis → ESP32-C6 + ESP-IDF ≥ 5.2)
- Interface web responsive + app mobile (BLE)
- OTA intégrée et profilage d'effets événementiels
- Passerelles CAN intégrées : GVRET TCP (SavvyCAN) + CANServer UDP
- ESP-NOW : rôle maître par défaut, profils satellites disponibles (blindspot, speedometer)

## ⚡ Démarrer vite
1. Cloner : `git clone https://github.com/raphaelgiga/car-light-sync.git`
2. Ouvrir le repo et installer **PlatformIO**.
3. Flasher : `pio run -e esp32c6 -t upload` puis `pio device monitor`.
4. Se connecter au WiFi `CarLightSync` et ouvrir `http://192.168.4.1`.
→ Détails et variantes ESP32-S3 : voir `docs/logiciel.md`.

## 📚 Documentation détaillée
- Matériel : `docs/materiel.md`
- Logiciel (build/flash, interface, OTA) : `docs/logiciel.md`
- Firmware & code (architecture, effets, CAN, audio) : `docs/firmware.md`
- Problèmes, dépannage & sécurité : `docs/problemes.md`

## Sources & références 🔗
- 🚗 DBC Tesla Model 3 : https://github.com/joshwardell/model3dbc (base des signaux CAN)
- 📕 Opendbc (DBC communautaires) : https://github.com/commaai/opendbc
- 🚙 Onyx M2 DBC : https://github.com/onyx-m2/onyx-m2-dbc (DBC Onyx M2)
- 🛰️ Protocole GVRET / SavvyCAN : https://github.com/collin80/SavvyCAN/blob/master/connections/gvretserial.cpp (passerelle TCP)
- 📡 CANserver (UDP Panda) : https://github.com/commaai/canserver (format et passerelle UDP)
- 📘 ESP-IDF SDK : https://docs.espressif.com/projects/esp-idf/en/stable/esp32/ (guide complet)

## 🤝 Contribution
- Fork, 
- branche `feature/...`, 
- PR. Zones utiles : configs CAN (autres véhicules), nouveaux effets LED, perfs, doc/traductions, tests.

## 📄 Licence
- voir `LICENSE`

## 💬 Support & communauté
- **Issues GitHub** : Pour signaler bugs et proposer fonctionnalités
- **Discussions** : Pour questions et partage d'expériences
- **Wiki** : Documentation communautaire et guides

---

**Développé avec ❤️ pour la communauté automobile**
