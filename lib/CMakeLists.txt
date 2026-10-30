# SPDX-License-Identifier: Apache-2.0

add_subdirectory(fw)
add_subdirectory(hal)
add_subdirectory_ifdef(CONFIG_GTEST gtest)
