# Installation Complète (Flash Initial)

Ce dossier contient tous les fichiers nécessaires pour flasher l'ESP32 depuis zéro.

## Fichiers inclus:
- **bootloader.bin** (offset: 0x1000) - Bootloader ESP32
- **partitions.bin** (offset: 0x8000) - Table des partitions
- **firmware.bin** (offset: 0x10000) - Application principale

## Méthode 1: Via PlatformIO
```bash
pio run -t upload
```

## Méthode 2: Via esptool.py
```bash
esptool.py --chip esp32 --port COM_PORT --baud 921600 \
  --before default_reset --after hard_reset write_flash -z \
  --flash_mode dio --flash_freq 40m --flash_size detect \
  0x1000 bootloader.bin \
  0x8000 partitions.bin \
  0x10000 firmware.bin
```

Remplacez `COM_PORT` par votre port série (ex: COM3, /dev/ttyUSB0)

## Méthode 3: Via ESP Flash Download Tool
1. Ouvrir ESP Flash Download Tool
2. Sélectionner ESP32
3. Ajouter les fichiers avec leurs offsets:
   - bootloader.bin @ 0x1000
   - partitions.bin @ 0x8000
   - firmware.bin @ 0x10000
4. Sélectionner le port COM
5. Cliquer sur START

---
Généré le: 2025-12-19 06:50:43
