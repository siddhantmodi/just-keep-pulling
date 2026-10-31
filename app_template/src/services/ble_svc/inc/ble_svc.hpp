/**
 * @file ble_svc.hpp
 * @brief BLE service public API: enables Bluetooth and advertises the app's GATT service.
 *
 * GATT service 9bb40001-b8c3-4eba-a5ac-7924595b107a:
 * - LED, 9bb40002-…: read/write, uint8, 0 = off, 1 = on. A successful write
 *   means the request was accepted; the app manager applies it shortly after.
 * - Die temperature, 9bb40003-…: read, int16 little-endian, hundredths of a
 *   degree Celsius.
 *
 * Open security: no pairing is required, so anyone in range can read and write.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fw/error.hpp>

namespace ble_svc
{

/** @brief Enable Bluetooth and start connectable advertising. Restarts advertising after each disconnect. */
[[nodiscard]] fw::Result<> init();

} // namespace ble_svc
