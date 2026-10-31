# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_temp_svc)

target_compile_options(app PRIVATE -Werror)

# The DieTemperature class only; the production instance (temp_svc.cpp) is not
# built, so no sensor driver is involved. The HAL mocks come from lib/hal.
target_include_directories(app PRIVATE ../inc ../src)

target_sources(app PRIVATE
    ../src/die_temperature.cpp
    src/test_die_temperature.cpp
)
