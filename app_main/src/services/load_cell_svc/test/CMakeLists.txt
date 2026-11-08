# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_load_cell_svc)

target_compile_options(app PRIVATE -Werror)

# The LoadCell class with a mocked ADC (lib/hal mocks) and real settings on
# native_sim's flash simulator; the production instance is not built.
target_include_directories(app PRIVATE ../inc ../src ../../../modules/calibration/inc)

target_sources(app PRIVATE
    ../src/load_cell.cpp
    ../../../modules/calibration/src/calibration.cpp
    src/test_load_cell.cpp
)
