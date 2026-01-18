# Car Light Sync — Troubleshooting & Security

Quick guide to resolve common issues and reminder of security best practices.

## Troubleshooting
### LEDs do not light up
- Check strip polarity (some swap red/black).
- Test with low power (3.3V) and a single LED.
- Confirm `LED_PIN` and `NUM_LEDS` in `include/config.h`.
- Add series resistor (330–470 Ω) and 1000 µF capacitor on power supply.

### No CAN messages received
- Check CAN_H/CAN_L wiring and common ground.
- Ensure transceiver is 3.3V and correctly powered.
- Confirm TX/RX GPIOs in `main/can_bus.c` and speed (500 kbit/s default).
- Test on a known bus (e.g. OBD) to isolate the problem.
- If you are on ESP32-S3 (only 1 TWAI), some functions requiring 2 buses will not be available; switch to ESP32-C6 for full functionality.

### Web interface inaccessible
- Connect to `CarLightSync` WiFi then open `http://192.168.4.1`.
- Cycle power after flash if AP does not appear.
- Check that no other device is using the same WiFi channel or IP.

## Security
- Change default passwords (`config.h`, `wifi_credentials.h`).
- Do not expose AP on an untrusted network; use VPN if necessary.
- Protect physical access to ESP32 and disable AP when not useful.
- The web interface is not password protected by default.

## Support & Community
- **GitHub Issues**: report bugs and propose features.
- **Discussions**: questions, feedback.
- **Wiki**: community documentation.
