# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_app_manager)

target_compile_options(app PRIVATE -Werror)

# The Manager class with a mocked LED (led_svc's MockLed) and the app's real
# zbus channels, so tests check what the manager publishes.
target_include_directories(app PRIVATE
    ../inc
    ../src
    ../../../channels/inc
    ../../../services/led_svc/inc
    ../../../services/led_svc/mocks
)

target_sources(app PRIVATE
    ../../../channels/src/channels.cpp
    ../src/manager.cpp
    src/test_manager.cpp
)
