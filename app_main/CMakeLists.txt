# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(app_main)

# Application code builds warning-free; dependencies keep their own flags.
target_compile_options(app PRIVATE -Werror)

target_sources(app PRIVATE src/main.cpp)

# zbus channels: the contracts shared by every layer
add_subdirectory_ifdef(CONFIG_ZBUS src/channels)

# Modules: pure logic, no Zephyr
add_subdirectory(src/modules/calibration)
add_subdirectory(src/modules/progressor)

# Services: one subsystem or peripheral each, no thread of their own
add_subdirectory_ifdef(CONFIG_APP_IDENTITY_SVC src/services/identity_svc)
add_subdirectory_ifdef(CONFIG_APP_LOAD_CELL_SVC src/services/load_cell_svc)
add_subdirectory_ifdef(CONFIG_APP_BLE_SVC src/services/ble_svc)

# Debug builds only (debug.conf)
add_subdirectory_ifdef(CONFIG_APP_DEBUG_SHELL src/debug_shell)
