# SPDX-License-Identifier: Apache-2.0

target_include_directories(app PRIVATE inc)

target_sources(app PRIVATE
    src/app.cpp
    src/app_manager.cpp
)
