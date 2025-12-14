# Car Light Sync — Firmware & Code

Notes techniques sur l’architecture, les effets et la logique CAN du projet.

## Architecture du code
- `include/` : headers, configuration matérielle et CAN.
- `main/` : pipeline CAN, effets LED, audio I2S, WiFi, OTA, web server, BLE optionnel.
- `data/` : interface web (HTML/JS/CSS) et icônes.
- `docs/` / `tools/` : documentation et scripts utilitaires.
- Fichiers générés : `vehicle_can_unified_config.generated.c/.h` (mapping CAN auto-généré).

## Configuration CAN multi-véhicules
- Architecture CAN unifiée basée sur DBC, avec décodage générique des signaux.
- Fichiers générés contiennent messages, signaux (start_bit, length, byte_order, factor, offset) et mapping des événements.
- Les événements CAN sont ensuite associés à des effets LED via la configuration.

## Effets LED disponibles
| ID | Nom | Description courte |
|----|-----|--------------------|
| OFF | Off | LEDs éteintes |
| SOLID | Solid | Couleur unie |
| BREATHING | Breathing | Respiration douce |
| RAINBOW | Rainbow | Arc-en-ciel statique |
| RAINBOW_CYCLE | Rainbow Cycle | Arc-en-ciel défilant |
| THEATER_CHASE | Theater Chase | Effet théâtre |
| RUNNING_LIGHTS | Running Lights | Lignes courantes |
| TWINKLE | Twinkle | Scintillement |
| FIRE | Fire | Simulation de feu |
| SCAN | Scan | Balayage type K2000 |
| KNIGHT_RIDER | Knight Rider | Variante K2000 |
| FADE | Fade | Fondu progressif |
| STROBE | Strobe | Stroboscope |
| VEHICLE_SYNC | Vehicle Sync | Sync état véhicule |
| TURN_SIGNAL | Turn Signal | Clignotant animé |
| BRAKE_LIGHT | Brake Light | Freinage |
| CHARGE_STATUS | Charge Status | État de charge |
| HAZARD | Hazard | Warning |
| BLINDSPOT_FLASH | Blindspot Flash | Flash angle mort |
| AUDIO_REACTIVE | Audio Reactive | VU-mètre audio |
| AUDIO_BPM | Audio BPM | Flash sur BPM |
| FFT_SPECTRUM | FFT Spectrum | Spectre FFT |
| FFT_BASS_PULSE | FFT Bass Pulse | Pulse basses |
| FFT_VOCAL_WAVE | FFT Vocal Wave | Onde voix |
| FFT_ENERGY_BAR | FFT Energy Bar | Barre d’énergie |
| COMET | Comet | Comète avec traînée |
| METEOR_SHOWER | Meteor Shower | Pluie de météores |
| RIPPLE_WAVE | Ripple Wave | Onde concentrique |
| DUAL_GRADIENT | Dual Gradient | Double dégradé |
| SPARKLE_OVERLAY | Sparkle Overlay | Fond + scintilles |
| CENTER_OUT_SCAN | Center Out Scan | Double scan centre→bords |

## Mode audio réactif (INMP441)
- Modulation audio sur tous les effets (10–100 %).
- VU-mètre, détection BPM (60–180 BPM) et analyse spectrale 3 bandes.
- Configuration matérielle : INMP441 connecté en I2S (pins configurables).
- Activation :
  1) Connecter le micro,
  2) Activer le micro dans l’interface web (onglet Configuration),
  3) Cocher “Mode Audio Réactif” dans l’onglet Profils.

## Événements CAN supportés (exemples)
- Signaux conduite : clignotants, warning, marche arrière, drive, park.
- Sécurité : angle mort gauche/droit, collisions latérales, collision avant.
- Véhicule : portes, verrouillage, vitesse seuil, autopilot on/off, lane departure.
- Énergie : charge en cours/terminée/démarrée/arrêtée, câble connecté/déconnecté, port de charge ouvert, sentry mode.

## Performances & spécifications
- 50 FPS (~20 ms/frame) pour les effets LED.
- Latence CAN < 100 ms.
- Audio temps réel ~50 Hz (tâche dédiée), RAM audio ~4 KB.
- OTA intégrée, compression JSON avec clés courtes, WiFi AP + STA, BLE optionnel.
- Priorisation d’effets (0–255) et effets temporaires avec retour à l’effet par défaut.
