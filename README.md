# Car Light Sync

WS2812 RGB LED control system synchronized to CAN bus, with web/mobile interface and OTA updates. Open source, non-profit, and community-oriented project.

## ☕ Support the project
Car Light Sync is maintained in my free time. You can help by:
- Starring the repository and sharing the project
- Contributing to code, docs, or tests (issues/PRs welcome)
- Buying a coffee to fund hardware, hosting, and prototypes: [![Buy Me A Coffee](https://www.buymeacoffee.com/assets/img/custom_images/orange_img.png)](https://buymeacoffee.com/raphael.d)

Thank you! Your support keeps the project free and accessible.

## 🚀 Quick Overview
- WS2812/WS2812B LEDs with audio-reactive effects
- Multi-vehicle CAN integration (unified architecture, dual TWAI required → ESP32-C6 + ESP-IDF ≥ 5.2)
- Responsive web interface + mobile app (BLE)
- Automotive dashboard with Park/Drive modes (speed display, pedal arc, WiFi-wave blindspot indicators)
- Integrated OTA and event-based effect profiling
- Integrated CAN gateways: GVRET TCP (SavvyCAN) + CANServer UDP
- ESP-NOW: Master role by default, satellite profiles available (blindspot, speedometer)

## ⚡ Quick Start
1. Clone: `git clone https://github.com/rafal83/car-light-sync.git`
2. Open the repo and install **PlatformIO**.
3. Flash: `pio run -e esp32c6 -t upload` then `pio device monitor`.
4. Connect to WiFi `CarLightSync` and open `http://192.168.4.1`.
→ Details and ESP32-S3 variants: see [docs/software.md](docs/software.md).

## 📚 Detailed Documentation
- Hardware: [docs/hardware.md](docs/hardware.md)
- Software (build/flash, interface, OTA): [docs/software.md](docs/software.md)
- Firmware & Code (architecture, effects, CAN, audio): [docs/firmware.md](docs/firmware.md)
- Troubleshooting & Security: [docs/troubleshooting.md](docs/troubleshooting.md)

## Sources & References 🔗
- 🚗 Tesla Model 3 DBC: https://github.com/joshwardell/model3dbc (base CAN signals)
- 📕 Opendbc (Community DBCs): https://github.com/commaai/opendbc
- 🚙 Onyx M2 DBC: https://github.com/onyx-m2/onyx-m2-dbc (Onyx M2 DBC)
- 🛰️ GVRET / SavvyCAN Protocol: https://github.com/collin80/SavvyCAN/blob/master/connections/gvretserial.cpp (TCP gateway)
- 📡 CANserver (UDP Panda): https://github.com/commaai/canserver (UDP format and gateway)
- 📘 ESP-IDF SDK: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/ (complete guide)

## 🤝 Contribution
- Fork,
- Branch `feature/...`,
- PR. Useful areas: CAN configs (other vehicles), new LED effects, performance, docs/translations, tests.

## 📄 License
- see [LICENSE](LICENSE)

## 💬 Support & Community
- **GitHub Issues**: To report bugs and propose features
- **Discussions**: For questions and sharing experiences
- **Wiki**: Community documentation and guides

---

**Developed with ❤️ for the automotive community**
