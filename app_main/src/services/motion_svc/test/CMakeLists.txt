# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_motion_svc)

target_compile_options(app PRIVATE -Werror)

# The Motion class with a mocked sensor and power switch (lib/hal's mocks).
target_include_directories(app PRIVATE ../inc ../src)

target_sources(app PRIVATE
    ../src/motion.cpp
    src/test_motion.cpp
)
