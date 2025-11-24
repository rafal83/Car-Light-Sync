# Gemini Assistant Project Guide

This document provides context and instructions for the Gemini assistant to effectively contribute to this project.

## Project Overview

This is an embedded project for ESP32 microcontrollers designed to control an LED strip based on data from a Tesla's CAN bus. It features a web interface and a companion mobile app for configuration and control.

## Core Technologies

- **Firmware:** C language, using the ESP-IDF framework and PlatformIO for building and dependency management.
- **CAN Bus:** Utilizes a `.dbc` file (`Model3CAN.dbc`) as the source of truth for CAN signals. Python scripts are used to generate C source code from this file.
- **Web Interface:** A single-page HTML/JS application served by the ESP32's web server. The HTML is compressed into a `.gz` file and embedded in the firmware.
- **Mobile App:** A Capacitor-based application located in the `mobile.app/` directory. It is a separate project with its own `package.json` and build process.
- **Tooling:** Python and shell scripts are used for various build and code generation tasks.

## Key Development Workflows

### 1. Building the Firmware

- The project is built using PlatformIO.
- The standard command to build the project is:
  ```bash
  pio run -e esp32s3
  ```
- Builds for other targets (`esp32dev`, `esp32s2`) can be run by changing the environment name.

### 2. Code Formatting

- The project uses `clang-format` to maintain a consistent code style.
- The configuration is defined in the `.clang-format` file at the project root.
- To format a file, use:
  ```bash
  clang-format -i path/to/your/file.c
  ```
- The primary target directories for formatting are `main/` and `include/`.

### 3. Modifying the Web Interface

- The web interface source file is `data/index.html`.
- Before building the firmware, this file is automatically compressed to `data/index.html.gz` by the `tools/tools-build/compress_html.py` script, which is triggered by the PlatformIO build process.
- **Instruction:** After modifying `data/index.html`, simply run the standard build command. The compression is handled automatically.

### 4. Modifying CAN Bus Signal Definitions

- The CAN signals are defined in `Model3CAN.dbc`.
- The C structures and definitions used in the firmware are generated automatically from this DBC file.
- The script `tools/can/generate_vehicle_can_config.py` is responsible for this generation.
- **Instruction:** If you update the `Model3CAN.dbc` file or need to change the CAN configuration, you **must** re-run the relevant Python script to regenerate the C code. The exact command should be verified within the `tools` directory, but it is likely:
  ```bash
  python tools/can/generate_vehicle_can_config.py
  ```

### 5. Mobile App Development

- The mobile app in `mobile.app/` is a separate project.
- To work on it, navigate to that directory and use `npm` or `yarn` commands as defined in its `package.json`.
- Changes in the firmware (especially the BLE API) may require corresponding changes in the mobile app.
