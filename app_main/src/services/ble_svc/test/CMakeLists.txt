# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_ble_svc)

target_compile_options(app PRIVATE -Werror)

# Only the advertised name is unit tested; the GATT adapter is covered by the
# Bumble integration tests (docs/architecture.md, rule 5).
target_include_directories(app PRIVATE
    ../src
    ../../identity_svc/inc
)

target_sources(app PRIVATE src/test_advertising.cpp)
