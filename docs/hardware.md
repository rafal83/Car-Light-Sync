# Car Light Sync — Hardware

Reference document for everything related to hardware: components, wiring, and safety checks before powering the system.

## Hardware Required
- **ESP32-C6 DevKitC (recommended and necessary)**: only the ESP32-C6 offers **2 TWAI interfaces** and, with ESP-IDF ≥ 5.2 (multi-controller support), enables all CAN functionalities (BODY + CHASSIS).
- **ESP32-S3 (fallback option)**: works with **only 1 CAN bus** → limited features (no dual bus). The second bus is automatically disabled in the code.
- **WS2812/WS2812B LED Strip**: 60-150 LEDs recommended.
  - ⚠️ **Wiring Warning**: some strips swap red/black (red = GND, black = +5V). Check before powering.
  - 🔧 **Test first** with 3.3V to validate polarity.
- **CAN Transceiver**: SN65HVD230, MCP2551, or 3.3V equivalent.
- **Vehicle CAN Connector**: Door/Pillar A cable (Tesla) or OBD/20-pin depending on model.
- **5V Power Supply**: 3–10A depending on strip length.
- **INMP441 Microphone (optional)**: for audio-reactive mode.
- **ESP-NOW satellites (optional)**: ESP32-C6 modules configured as slaves (PlatformIO profiles `esp32c6_bll`, `esp32c6_blr`, `esp32c6_speedometer`) to offload blindspot or speedometer functions.

## LED Wiring
- Default `LED_PIN = 5` and `NUM_LEDS = 112` (adapt in `include/config.h`).
- Use 18–22 AWG wire for +5V and GND.
- Add a 1000 µF capacitor (5–16V) between +5V/GND on the strip side and a 330–470 Ω series resistor on the data line.

## CAN Connection
- Default GPIO: `CONFIG_CAN_TX_GPIO = 8`, `CONFIG_CAN_RX_GPIO = 7` (configurable in `main/can_bus.c`).
- Typical Transceiver:
  - ESP32 TX → Transceiver TX
  - ESP32 RX → Transceiver RX
  - 3V3 → Transceiver VCC, common GND
  - CAN_H/CAN_L → Vehicle CAN bus (parallel connection, non-invasive)
- Default Speed: 500 kbit/s (adapt according to vehicle if needed).

## Indicator LED and Reset Button
- Integrated Status LED:
  - ESP32-S3: GPIO 21
  - ESP32-C6: GPIO 8
- Reset Button (GPIO 4):
  - 5s Press = **factory reset** (erases NVS, profiles, WiFi).

## Useful CAN Locations (e.g. Tesla)
1. OBD-II Port (6 = CAN_H, 14 = CAN_L, 4/5 = GND)
2. Connector behind Media Center (Model 3/Y)
3. Connector under Driver Seat (Model S/X)

**Important**: always check polarity and continuity before connecting main power.
