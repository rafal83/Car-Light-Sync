# Car Light Sync — Firmware & Code

Technical notes on architecture, effects, and CAN logic of the project.

## Code Architecture
- `include/`: headers, hardware configuration, and CAN.
- `main/`: CAN pipeline, LED effects, I2S audio, WiFi, OTA, web server, optional BLE.
- `data/`: web interface (HTML/JS/CSS) and icons.
- `docs/` / `tools/`: documentation and utility scripts.
- Generated files: `vehicle_can_unified_config.generated.c/.h` (auto-generated CAN mapping).

## Multi-Vehicle CAN Configuration
- Unified CAN architecture based on DBC, with generic signal decoding.
- Generated files contain messages, signals (start_bit, length, byte_order, factor, offset), and event mapping.
- CAN events are then associated with LED effects via configuration.
- To benefit from **all** CAN features (dual bus), an **ESP32-C6** board is required **and ESP-IDF ≥ 5.2** (multi-controller TWAI support). On ESP32-S3 or without multi-controller, only one bus is active and the second is automatically disabled in firmware.

## CAN Gateways & Services
- **GVRET TCP (port 23)**: SavvyCAN compatible, exposes frames from both buses.
- **CANServer UDP**: Panda/UDS format, UDP frame broadcast.
- Configurable autostart for each service (see web API / interface).

## Network & OTA
- Web server with REST API, embedded web interface, OTA upload, and reboot.
- BLE support for mobile API (optional depending on build).
- CPU/memory status exposed via web API.

## Advanced Controls
- **Scroll Wheel**: profile change (opt-in) with speed threshold for safety.
- **Dynamic Brightness**: can follow vehicle brightness (`dynamic_brightness_enabled` flag in profiles).
- Effect priorities, temporary effects, and normalized segments to avoid strip overrun.

## Available LED Effects
| ID | Name | Short Description |
|----|-----|-------------------|
| OFF | Off | LEDs off |
| SOLID | Solid | Solid color |
| BREATHING | Breathing | Gentle breathing |
| RAINBOW | Rainbow | Static rainbow |
| RAINBOW_CYCLE | Rainbow Cycle | Moving rainbow |
| THEATER_CHASE | Theater Chase | Theater effect |
| RUNNING_LIGHTS | Running Lights | Running lines |
| TWINKLE | Twinkle | Twinkle |
| FIRE | Fire | Fire simulation |
| SCAN | Scan | K2000/Knight Rider type scan |
| KNIGHT_RIDER | Knight Rider | Knight Rider variant |
| FADE | Fade | Progressive fade |
| STROBE | Strobe | Strobe |
| VEHICLE_SYNC | Vehicle Sync | Vehicle state sync |
| TURN_SIGNAL | Turn Signal | Animated turn signal |
| BRAKE_LIGHT | Brake Light | Braking |
| CHARGE_STATUS | Charge Status | Charging status |
| HAZARD | Hazard | Warning/Hazards |
| BLINDSPOT_FLASH | Blindspot Flash | Blind spot flash |
| AUDIO_REACTIVE | Audio Reactive | Audio VU meter |
| AUDIO_BPM | Audio BPM | Flash on BPM |
| FFT_SPECTRUM | FFT Spectrum | FFT Spectrum |
| FFT_BASS_PULSE | FFT Bass Pulse | Bass pulse |
| FFT_VOCAL_WAVE | FFT Vocal Wave | Vocal wave |
| FFT_ENERGY_BAR | FFT Energy Bar | Energy bar |
| COMET | Comet | Comet with trail |
| METEOR_SHOWER | Meteor Shower | Meteor shower |
| RIPPLE_WAVE | Ripple Wave | Concentric wave |
| DUAL_GRADIENT | Dual Gradient | Dual gradient |
| SPARKLE_OVERLAY | Sparkle Overlay | Background + sparkles |
| CENTER_OUT_SCAN | Center Out Scan | Dual scan center→edges |

## Audio Reactive Mode (INMP441)
- Audio modulation on all effects (10–100%).
- VU meter, BPM detection (60–180 BPM), and 3-band spectral analysis.
- Hardware configuration: INMP441 connected via I2S (configurable pins).
- Activation:
  1) Connect microphone,
  2) Enable microphone in web interface (Configuration tab),
  3) Check “Audio Reactive Mode” in Profiles tab.

## Supported CAN Events (examples)
- Driving signals: turn signals, hazard, reverse, drive, park.
- Safety: left/right blind spot, side collisions, forward collision.
- Vehicle: doors, locks, speed threshold, autopilot on/off, lane departure.
- Energy: charging in progress/done/started/stopped, cable connected/disconnected, charge port open, sentry mode.

## Performance & Specifications
- 50 FPS (~20 ms/frame) for LED effects.
- CAN latency < 100 ms.
- Real-time Audio ~50 Hz (dedicated task), Audio RAM ~4 KB.
- Integrated OTA, JSON compression with short keys, WiFi AP + STA, optional BLE.
- Effect prioritization (0–255) and temporary effects with return to default effect.
