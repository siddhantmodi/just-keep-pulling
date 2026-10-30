# SPDX-License-Identifier: Apache-2.0
#
# Header-only hardware capability concepts (<hal/...>), their thin Zephyr
# implementations (<hal/zephyr/...>) and, for unit tests, their GoogleMocks
# (<hal/mocks/...>).

if(CONFIG_FW_HAL)
    zephyr_include_directories(include)
    if(CONFIG_GTEST)
        zephyr_include_directories(mocks/include)
    endif()
endif()
