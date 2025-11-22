cmake_minimum_required(VERSION 3.16)

# Skip unused components that trigger toolchain issues on some targets (e.g. ESP32-S3)
set(EXCLUDE_COMPONENTS esp_lcd)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(car-light-sync)
