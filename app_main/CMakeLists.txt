# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(app_main)

# Application code builds warning-free; dependencies keep their own flags.
target_compile_options(app PRIVATE -Werror)

target_sources(app PRIVATE src/main.cpp)

# zbus channels: the contracts shared by every layer
add_subdirectory_ifdef(CONFIG_ZBUS src/channels)

# Services: one subsystem or peripheral each, no thread of their own
add_subdirectory_ifdef(CONFIG_APP_LED_SVC src/services/led_svc)
add_subdirectory_ifdef(CONFIG_APP_TEMP_SVC src/services/temp_svc)
add_subdirectory_ifdef(CONFIG_APP_BLE_SVC src/services/ble_svc)

# Managers: own a thread and an event queue; the only callers of single-threaded services
add_subdirectory_ifdef(CONFIG_APP_MANAGER src/managers/app_manager)
