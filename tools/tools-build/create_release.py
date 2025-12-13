#!/usr/bin/env python3
"""
Script pour créer un package de release avec tous les fichiers nécessaires
pour l'installation et la mise à jour OTA
"""

import os
import shutil
import datetime
import subprocess

# Configuration
_requested_env = os.environ.get("PIOENV") or os.environ.get("CAR_LIGHT_SYNC_BUILD_ENV")
BUILD_DIR = os.path.join(".pio", "build", _requested_env) if _requested_env else ".pio/build/esp32c6"
RELEASE_DIR = "build"
PROJECT_NAME = "car-light-sync"


def get_version_string():
    try:
        commit_count = subprocess.check_output(
            ["git", "rev-list", "--count", "HEAD"],
            stderr=subprocess.STDOUT,
            encoding="utf-8"
        ).strip()
    except Exception:
        commit_count = "0"

    today = datetime.datetime.utcnow().isocalendar()
    year = today[0]
    week = today[1]

    version = f"{year}.{week:02d}.{int(commit_count):d}"

    return version


FIRMWARE_VERSION = get_version_string()

def create_release_package():
    """Crée le package de release avec tous les fichiers nécessaires"""

    # Créer le dossier de release
    if os.path.exists(RELEASE_DIR):
        shutil.rmtree(RELEASE_DIR)
    os.makedirs(RELEASE_DIR)

    print(f"📦 Création du package de release dans '{RELEASE_DIR}'...\n")

    # 1. Fichier pour l'installation complète (flash initial)
    print("1️⃣  Copie des fichiers pour l'installation complète...")
    flash_dir = os.path.join(RELEASE_DIR, "flash-complete")
    os.makedirs(flash_dir)

    files_to_copy = {
        "bootloader.bin": "0x1000",
        "partitions.bin": "0x8000",
        "firmware.bin": "0x10000"
    }

    for filename, offset in files_to_copy.items():
        src = os.path.join(BUILD_DIR, filename)
        if os.path.exists(src):
            dst = os.path.join(flash_dir, filename)
            shutil.copy2(src, dst)
            print(f"   ✓ {filename} (offset: {offset})")
        else:
            print(f"   ✗ {filename} non trouvé!")

    # 2. Fichier pour la mise à jour OTA
    print("\n2️⃣  Copie du fichier pour la mise à jour OTA...")
    ota_dir = os.path.join(RELEASE_DIR, "ota-update")
    os.makedirs(ota_dir)

    firmware_src = os.path.join(BUILD_DIR, "firmware.bin")
    size_mb = None
    if os.path.exists(firmware_src):
        firmware_dst = os.path.join(ota_dir, f"{PROJECT_NAME}-ota.bin")
        shutil.copy2(firmware_src, firmware_dst)
        size_mb = os.path.getsize(firmware_src) / (1024 * 1024)
        print(f"   ✓ {PROJECT_NAME}-ota.bin ({size_mb:.2f} MB)")
    else:
        print(f"   ✗ firmware.bin non trouvé!")

    # 3. Créer un fichier README avec les instructions
    print("\n3️⃣  Création du fichier README...")

    readme_flash = f"""# Installation Complète (Flash Initial)

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
esptool.py --chip esp32 --port COM_PORT --baud 921600 \\
  --before default_reset --after hard_reset write_flash -z \\
  --flash_mode dio --flash_freq 40m --flash_size detect \\
  0x1000 bootloader.bin \\
  0x8000 partitions.bin \\
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
Généré le: {datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")}
"""

    firmware_size_display = f"{size_mb:.2f}" if size_mb is not None else "N/A"

    readme_ota = f"""# Mise à Jour OTA (Over-The-Air)

Ce dossier contient le fichier firmware pour une mise à jour sans fil.

## Fichier inclus:
- **{PROJECT_NAME}-ota.bin** - Firmware pour mise à jour OTA

## Instructions:

### Via l'interface Web:
1. Connectez-vous au WiFi de l'ESP32 (SSID: CarLightSync)
2. Ouvrez un navigateur et allez à: http://192.168.4.1
3. Allez dans la section "🔄 Mise à Jour OTA"
4. Sélectionnez le fichier `{PROJECT_NAME}-ota.bin`
5. Cliquez sur "Téléverser"
6. Attendez la fin de l'upload (progression affichée)
7. Cliquez sur "Redémarrer" pour appliquer la mise à jour

### Via cURL (ligne de commande):
```bash
curl -F "firmware=@{PROJECT_NAME}-ota.bin" http://192.168.4.1/api/ota/upload
curl -X POST http://192.168.4.1/api/ota/restart
```

## Notes:
- Taille du firmware: ~{firmware_size_display} MB
- Durée estimée de l'upload: 30-60 secondes
- L'ESP32 redémarrera automatiquement après la mise à jour
- En cas d'échec, l'ESP32 reviendra automatiquement à la version précédente (rollback)

## Vérification de la version:
```bash
curl http://192.168.4.1/api/ota/info
```

---
Généré le: {datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")}
"""

    with open(os.path.join(flash_dir, "README.md"), "w", encoding="utf-8") as f:
        f.write(readme_flash)
    print(f"   ✓ README.md créé dans flash-complete/")

    with open(os.path.join(ota_dir, "README.md"), "w", encoding="utf-8") as f:
        f.write(readme_ota)
    print(f"   ✓ README.md créé dans ota-update/")

    # 4. Créer un script de flash automatique
    print("\n4️⃣  Création des scripts de flash...")

    # Script Windows
    flash_script_win = f"""@echo off
echo ================================
echo Installation ESP32 Car Light Sync
echo ================================
echo.

set /p PORT="Entrez le port COM (ex: COM3): "

echo.
echo Flashage en cours...
esptool.py --chip esp32 --port %PORT% --baud 921600 ^
  --before default_reset --after hard_reset write_flash -z ^
  --flash_mode dio --flash_freq 40m --flash_size detect ^
  0x1000 bootloader.bin ^
  0x8000 partitions.bin ^
  0x10000 firmware.bin

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ================================
    echo Flash termine avec succes!
    echo ================================
) else (
    echo.
    echo ================================
    echo Erreur lors du flash!
    echo ================================
)

pause
"""

    # Script Linux/Mac
    flash_script_unix = f"""#!/bin/bash

echo "================================"
echo "Installation ESP32 Car Light Sync"
echo "================================"
echo ""

read -p "Entrez le port série (ex: /dev/ttyUSB0): " PORT

echo ""
echo "Flashage en cours..."
esptool.py --chip esp32 --port $PORT --baud 921600 \\
  --before default_reset --after hard_reset write_flash -z \\
  --flash_mode dio --flash_freq 40m --flash_size detect \\
  0x1000 bootloader.bin \\
  0x8000 partitions.bin \\
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
"""

    with open(os.path.join(flash_dir, "flash.bat"), "w", encoding="utf-8") as f:
        f.write(flash_script_win)
    print(f"   ✓ flash.bat créé (Windows)")

    with open(os.path.join(flash_dir, "flash.sh"), "w", encoding="utf-8") as f:
        f.write(flash_script_unix)
    # Rendre le script exécutable sous Unix
    try:
        os.chmod(os.path.join(flash_dir, "flash.sh"), 0o755)
    except:
        pass
    print(f"   ✓ flash.sh créé (Linux/Mac)")

    # 5. Créer un fichier de version
    print("\n5️⃣  Création du fichier de version...")

    version_info = f"""Car Light Sync - Package de Release
=============================================

Version: {FIRMWARE_VERSION}
Date: {datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")}

Contenu:
--------
- flash-complete/  : Fichiers pour l'installation complète (flash initial)
- ota-update/      : Fichier pour la mise à jour OTA (sans fil)

Configuration:
--------------
- WiFi AP SSID    : CarLightSync
- WiFi AP Password: 
- Interface Web   : http://192.168.4.1
- Nombre de LEDs  : Configurable dans config.h

Fonctionnalités:
----------------
✓ Contrôle de bande LED WS2812B
✓ 16 effets lumineux différents
✓ Interface web de configuration
✓ Mise à jour OTA (Over-The-Air)
✓ Support CAN bus véhicule (Tesla et autres)
✓ Profils de configuration
✓ Événements CAN personnalisables

Pour plus d'informations:
-------------------------
Consultez les fichiers README.md dans chaque dossier.
"""

    with open(os.path.join(RELEASE_DIR, "VERSION.txt"), "w", encoding="utf-8") as f:
        f.write(version_info)
    print(f"   ✓ VERSION.txt créé")

    # Résumé
    print("\n" + "="*50)
    print("✅ Package de release créé avec succès!")
    print("="*50)
    print(f"\n📁 Dossier: {RELEASE_DIR}/")
    print(f"   ├── flash-complete/")
    print(f"   │   ├── bootloader.bin")
    print(f"   │   ├── partitions.bin")
    print(f"   │   ├── firmware.bin")
    print(f"   │   ├── flash.bat (Windows)")
    print(f"   │   ├── flash.sh (Linux/Mac)")
    print(f"   │   └── README.md")
    print(f"   ├── ota-update/")
    print(f"   │   ├── {PROJECT_NAME}-ota.bin")
    print(f"   │   └── README.md")
    print(f"   └── VERSION.txt")
    print()

if __name__ == "__main__":
    try:
        create_release_package()
    except Exception as e:
        print(f"\n❌ Erreur: {e}")
        import traceback
        traceback.print_exc()
