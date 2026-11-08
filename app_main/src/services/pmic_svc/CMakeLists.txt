# SPDX-License-Identifier: Apache-2.0

target_include_directories(app PRIVATE inc)

target_sources(app PRIVATE
    src/pmic.cpp
    src/pmic_svc.cpp
)
