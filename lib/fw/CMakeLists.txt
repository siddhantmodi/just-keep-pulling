# SPDX-License-Identifier: Apache-2.0
#
# C++ helpers shared by every app and test in this repo. Header-only, except
# fw::settings, which is built when the app enables CONFIG_SETTINGS.

if(CONFIG_FW_CPP)
    zephyr_include_directories(include)

    if(CONFIG_SETTINGS)
        zephyr_library()
        zephyr_library_sources(src/settings.cpp)
    endif()
endif()
