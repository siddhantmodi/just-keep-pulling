# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_fw)

target_compile_options(app PRIVATE -Werror)

target_sources(app PRIVATE
    src/test_error.cpp
    src/test_channel_ref.cpp
    src/test_scoped_lock.cpp
    src/test_queue_ref.cpp
    src/test_settings.cpp
)
