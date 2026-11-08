# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_identity_svc)

target_compile_options(app PRIVATE -Werror)

# The Identity class with real settings (native_sim's flash simulator) and the
# real random number generator; the production instance is not built.
target_include_directories(app PRIVATE ../inc ../src)

target_sources(app PRIVATE
    ../src/identity.cpp
    src/test_identity.cpp
)
