# SPDX-License-Identifier: Apache-2.0

target_include_directories(app PRIVATE inc)

# The core, then one file per GATT service (the Device Information service is all Kconfig).
target_sources(app PRIVATE
    src/ble_svc.cpp
    src/tindeq_service.cpp
)
target_sources_ifdef(CONFIG_APP_BLE_SVC_JKP app PRIVATE src/jkp_service.cpp)
target_sources_ifdef(CONFIG_BT_BAS app PRIVATE src/battery_service.cpp)
