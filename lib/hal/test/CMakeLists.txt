# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_hal)

target_compile_options(app PRIVATE -Werror)

target_sources(app PRIVATE
    src/test_gpio_output.cpp
    src/test_sensor_temperature.cpp
    src/test_ads1220.cpp
    src/test_npm13xx.cpp
    src/test_sensor_motion.cpp
)
