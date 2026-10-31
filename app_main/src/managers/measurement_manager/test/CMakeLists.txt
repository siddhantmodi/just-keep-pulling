# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_measurement_manager)

target_compile_options(app PRIVATE -Werror)

# The Manager class with a mocked load cell and client (each component's own
# mock) and the real progressor encoding, so tests check the bytes sent.
target_include_directories(app PRIVATE
    ../inc
    ../src
    ../../../channels/inc
    ../../../modules/progressor/inc
    ../../../services/ble_svc/inc
    ../../../services/ble_svc/mocks
    ../../../services/load_cell_svc/inc
    ../../../services/load_cell_svc/mocks
)

target_sources(app PRIVATE
    ../../../channels/src/channels.cpp
    ../../../modules/progressor/src/progressor.cpp
    ../src/manager.cpp
    src/test_manager.cpp
)
