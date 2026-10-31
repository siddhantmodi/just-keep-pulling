# SPDX-License-Identifier: Apache-2.0
#
# Zephyr module entry point (see zephyr/module.yml). Apps live in their own
# directories (e.g. app_main/) and are not built from here.

zephyr_include_directories(include)

add_subdirectory(drivers)
add_subdirectory(lib)
