#!/bin/bash

echo "================================"
echo "Installation ESP32 Car Light Sync"
echo "================================"
echo ""

read -p "Entrez le port série (ex: /dev/ttyUSB0): " PORT

echo ""
echo "Flashage en cours..."
esptool.py --chip esp32 --port $PORT --baud 921600 \
  --before default_reset --after hard_reset write_flash -z \
  --flash_mode dio --flash_freq 40m --flash_size detect \
  0x1000 bootloader.bin \
  0x8000 partitions.bin \
  0x10000 firmware.bin

if [ $? -eq 0 ]; then
    echo ""
    echo "================================"
    echo "Flash terminé avec succès!"
    echo "================================"
else
    echo ""
    echo "================================"
    echo "Erreur lors du flash!"
    echo "================================"
fi
