# SPDX-License-Identifier: Apache-2.0

target_include_directories(app PRIVATE inc)

target_sources(app PRIVATE
    src/manager.cpp
    src/measurement_manager.cpp
)
