# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_led_svc)

target_compile_options(app PRIVATE -Werror)

# led_svc::Led is header-only over hal::DigitalOutput, so the test needs only the
# component's headers and the HAL mocks; no GPIO driver is compiled in.
target_include_directories(app PRIVATE ../inc ../src)

target_sources(app PRIVATE src/test_led.cpp)
