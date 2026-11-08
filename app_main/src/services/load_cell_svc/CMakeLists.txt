# SPDX-License-Identifier: Apache-2.0

target_include_directories(app PRIVATE inc)

target_sources(app PRIVATE
    src/load_cell.cpp
    src/load_cell_svc.cpp
)
