# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_power_manager)

target_compile_options(app PRIVATE -Werror)

# The Power and Activity classes with a mocked PMIC, IMU and client (each
# component's own mock), the app's real zbus channels and the real progressor
# encoding.
target_include_directories(app PRIVATE
    ../inc
    ../src
    ../../../channels/inc
    ../../../modules/progressor/inc
    ../../../services/ble_svc/inc
    ../../../services/ble_svc/mocks
    ../../../services/motion_svc/inc
    ../../../services/motion_svc/mocks
    ../../../services/pmic_svc/inc
    ../../../services/pmic_svc/mocks
)

target_sources(app PRIVATE
    ../../../channels/src/channels.cpp
    ../../../modules/progressor/src/progressor.cpp
    ../src/activity.cpp
    ../src/power.cpp
    src/test_activity.cpp
    src/test_power.cpp
)
