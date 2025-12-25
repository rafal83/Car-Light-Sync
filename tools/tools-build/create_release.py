#!/usr/bin/env python3
"""
Script to create a release package with all necessary files
for installation and OTA updates
"""

import os
import shutil
import datetime
import subprocess

# Configuration
_requested_env = os.environ.get("PIOENV") or os.environ.get("CAR_LIGHT_SYNC_BUILD_ENV")
BUILD_DIR = os.path.join(".pio", "build", _requested_env) if _requested_env else ".pio/build/esp32c6"
RELEASE_DIR = "release"
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
    """Creates the release package with all necessary files"""

    # Create release folder
    if os.path.exists(RELEASE_DIR):
        shutil.rmtree(RELEASE_DIR)
    os.makedirs(RELEASE_DIR)

    print(f"Creating release package in '{RELEASE_DIR}'...\n")

    # 1. Files for complete installation (initial flash)
    print("1. Copying files for complete installation...")
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
            print(f"   - {filename} (offset: {offset})")
        else:
            print(f"   x {filename} not found!")

    # 2. File for OTA update
    print("\n2. Copying file for OTA update...")
    ota_dir = os.path.join(RELEASE_DIR, "ota-update")
    os.makedirs(ota_dir)

    firmware_src = os.path.join(BUILD_DIR, "firmware.bin")
    size_mb = None
    if os.path.exists(firmware_src):
        firmware_dst = os.path.join(ota_dir, f"{PROJECT_NAME}-ota.bin")
        shutil.copy2(firmware_src, firmware_dst)
        size_mb = os.path.getsize(firmware_src) / (1024 * 1024)
        print(f"   - {PROJECT_NAME}-ota.bin ({size_mb:.2f} MB)")
    else:
        print(f"   x firmware.bin not found!")

    # 3. Create README file with instructions
    print("\n3. Creating README file...")

    readme_flash = f"""# Complete Installation (Initial Flash)

This folder contains all the necessary files to flash the ESP32 from scratch.

## Included files:
- **bootloader.bin** (offset: 0x1000) - ESP32 Bootloader
- **partitions.bin** (offset: 0x8000) - Partition table
- **firmware.bin** (offset: 0x10000) - Main application

## Method 1: Via PlatformIO
```bash
pio run -t upload
```

## Method 2: Via esptool.py
```bash
esptool.py --chip esp32 --port COM_PORT --baud 921600 \\
  --before default_reset --after hard_reset write_flash -z \\
  --flash_mode dio --flash_freq 40m --flash_size detect \\
  0x1000 bootloader.bin \\
  0x8000 partitions.bin \\
  0x10000 firmware.bin
```

Replace `COM_PORT` with your serial port (e.g. COM3, /dev/ttyUSB0)

## Method 3: Via ESP Flash Download Tool
1. Open ESP Flash Download Tool
2. Select ESP32
3. Add files with their offsets:
   - bootloader.bin @ 0x1000
   - partitions.bin @ 0x8000
   - firmware.bin @ 0x10000
4. Select COM port
5. Click START

---
Generated on: {datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")}
"""

    firmware_size_display = f"{size_mb:.2f}" if size_mb is not None else "N/A"

    readme_ota = f"""# OTA Update (Over-The-Air)

This folder contains the firmware file for wireless update.

## Included file:
- **{PROJECT_NAME}-ota.bin** - Firmware for OTA update

## Instructions:

### Via Web Interface:
1. Connect to ESP32 WiFi (SSID: CarLightSync)
2. Open a browser and go to: http://192.168.4.1
3. Go to "OTA Update" section
4. Select the file `{PROJECT_NAME}-ota.bin`
5. Click "Upload"
6. Wait for upload completion (progress displayed)
7. Click "Restart" to apply the update

### Via cURL (command line):
```bash
curl -F "firmware=@{PROJECT_NAME}-ota.bin" http://192.168.4.1/api/ota/upload
curl -X POST http://192.168.4.1/api/ota/restart
```

## Notes:
- Firmware size: ~{firmware_size_display} MB
- Estimated upload time: 30-60 seconds
- ESP32 will restart automatically after update
- In case of failure, ESP32 will automatically rollback to previous version

## Version check:
```bash
curl http://192.168.4.1/api/ota/info
```

---
Generated on: {datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")}
"""

    with open(os.path.join(flash_dir, "README.md"), "w", encoding="utf-8") as f:
        f.write(readme_flash)
    print(f"   - README.md created in flash-complete/")

    with open(os.path.join(ota_dir, "README.md"), "w", encoding="utf-8") as f:
        f.write(readme_ota)
    print(f"   - README.md created in ota-update/")

    # 4. Create automatic flash scripts
    print("\n4. Creating flash scripts...")

    # Windows script
    flash_script_win = f"""@echo off
echo ================================
echo ESP32 Car Light Sync Installation
echo ================================
echo.

set /p PORT="Enter COM port (e.g. COM3): "

echo.
echo Flashing in progress...
esptool.py --chip esp32 --port %PORT% --baud 921600 ^
  --before default_reset --after hard_reset write_flash -z ^
  --flash_mode dio --flash_freq 40m --flash_size detect ^
  0x1000 bootloader.bin ^
  0x8000 partitions.bin ^
  0x10000 firmware.bin

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ================================
    echo Flash completed successfully!
    echo ================================
) else (
    echo.
    echo ================================
    echo Flash error!
    echo ================================
)

pause
"""

    # Linux/Mac script
    flash_script_unix = f"""#!/bin/bash

echo "================================"
echo "ESP32 Car Light Sync Installation"
echo "================================"
echo ""

read -p "Enter serial port (e.g. /dev/ttyUSB0): " PORT

echo ""
echo "Flashing in progress..."
esptool.py --chip esp32 --port $PORT --baud 921600 \\
  --before default_reset --after hard_reset write_flash -z \\
  --flash_mode dio --flash_freq 40m --flash_size detect \\
  0x1000 bootloader.bin \\
  0x8000 partitions.bin \\
  0x10000 firmware.bin

if [ $? -eq 0 ]; then
    echo ""
    echo "================================"
    echo "Flash completed successfully!"
    echo "================================"
else
    echo ""
    echo "================================"
    echo "Flash error!"
    echo "================================"
fi
"""

    with open(os.path.join(flash_dir, "flash.bat"), "w", encoding="utf-8") as f:
        f.write(flash_script_win)
    print(f"   - flash.bat created (Windows)")

    with open(os.path.join(flash_dir, "flash.sh"), "w", encoding="utf-8") as f:
        f.write(flash_script_unix)
    # Make script executable on Unix
    try:
        os.chmod(os.path.join(flash_dir, "flash.sh"), 0o755)
    except:
        pass
    print(f"   - flash.sh created (Linux/Mac)")

    # 5. Create version file
    print("\n5. Creating version file...")

    version_info = f"""Car Light Sync - Release Package
=============================================

Version: {FIRMWARE_VERSION}
Date: {datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")}

Contents:
--------
- flash-complete/  : Files for complete installation (initial flash)
- ota-update/      : File for OTA update (wireless)

Configuration:
--------------
- WiFi AP SSID    : CarLightSync
- WiFi AP Password:
- Web Interface   : http://192.168.4.1
- Number of LEDs  : Configurable in config.h

Features:
----------------
- WS2812B LED strip control
- 16 different light effects
- Web configuration interface
- OTA updates (Over-The-Air)
- Vehicle CAN bus support (Tesla and others)
- Configuration profiles
- Customizable CAN events

For more information:
-------------------------
See README.md files in each folder.
"""

    with open(os.path.join(RELEASE_DIR, "VERSION.txt"), "w", encoding="utf-8") as f:
        f.write(version_info)
    print(f"   - VERSION.txt created")

    # Summary
    print("\n" + "="*50)
    print("Release package created successfully!")
    print("="*50)
    print(f"\nFolder: {RELEASE_DIR}/")
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
        print(f"\nError: {e}")
        import traceback
        traceback.print_exc()
