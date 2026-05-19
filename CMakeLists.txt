# AquaControl — ESP-IDF top-level CMake
# Target: ESP32-S3 (ElecRow 5" display, ILI6122/ILI5960 RGB, GT911 touch)

cmake_minimum_required(VERSION 3.16)

# Pull in extra component directories
set(EXTRA_COMPONENT_DIRS "components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(aquacontrol
    VERSION 0.1.0
    LANGUAGES C CXX
)
