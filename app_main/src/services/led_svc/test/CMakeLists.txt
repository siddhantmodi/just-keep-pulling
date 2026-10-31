# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_led_svc)

target_compile_options(app PRIVATE -Werror)

# The Led class only; the production instance (led_svc.cpp) is not built, so
# no GPIO driver is involved. The HAL mocks come from lib/hal.
target_include_directories(app PRIVATE ../inc ../src)

target_sources(app PRIVATE
    ../src/led.cpp
    src/test_led.cpp
)
