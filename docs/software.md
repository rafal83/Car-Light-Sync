# Car Light Sync — Software

This document covers prerequisites, installation, and basic software usage (without detailing the JSON API).

## Software Prerequisites
- **PlatformIO** (recommended) or **ESP-IDF v5.2+** (v5.2 required for multi-controller TWAI support)
- **Python 3.7+**
- Git tools to clone the repository

## Compilation & Flash (PlatformIO)
```bash
# Clone
git clone https://github.com/rafal83/car-light-sync.git
cd car-light-sync

# Compile and flash (ESP32-C6 profile, the only one offering 2 TWAI and all CAN features)
pio run -e esp32c6 -t upload
pio device monitor
```
> ESP32-S3: possible with `-e esp32s3` or `-e esp32s3_n4r2`, but limited to 1 CAN bus (not all features).
> ESP-NOW Satellite Profiles: `-e esp32c6_bll`, `-e esp32c6_blr`, `-e esp32c6_speedometer` (slave side). Without these profiles, the firmware remains ESP-NOW Master by default.

## Compilation & Flash (ESP-IDF)
```bash
# Install ESP-IDF if needed
git clone --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf
cd ~/esp/esp-idf && ./install.sh esp32c6 && . ./export.sh

# Clone and build project
git clone https://github.com/rafal83/car-light-sync.git ~/projects/car-light-sync
cd ~/projects/car-light-sync
idf.py build
idf.py -p <port> flash monitor
```
> Note: Dual CAN bus (BODY + CHASSIS) requires ESP-IDF ≥ 5.2 and a target with 2 TWAI controllers (ESP32-C6). Otherwise, the second bus is automatically disabled in firmware.

## Initial Configuration
1. **Hardware**: edit `include/config.h` (`LED_PIN`, `NUM_LEDS`, CAN pins...).
2. **WiFi (optional)**: fill in `include/wifi_credentials.h`.
3. **CAN Pins**: adjust in `main/can_bus.c` if necessary.

## Web Interface
- Default Access Point: `CarLightSync` (no password), URL `http://192.168.4.1`.
- Real-time Control: effect, color, brightness, speed, profiles, and CAN events.
- Integrated OTA: upload firmware directly from the interface.
- Service Control: start/stop GVRET TCP, CANServer UDP, manage autostart.
- System Telemetry: CPU, memory, CAN status, NVS storage.

## Mobile Application
- iOS/Android App (Capacitor) reusing `data/index.html`, `data/script.js`, `data/style.css`.
- Auto-connection via BLE to control the module without WiFi.

## OTA Updates
From the web interface: upload the compiled binary (`.bin`) via the OTA section.
