/**
 * @file tindeq_service.hpp
 * @brief What ble_svc's core needs from the Tindeq Progressor GATT service
 *        (tindeq_service.cpp): its UUID, advertised in the scan response, and
 *        the data point that Client::send_progressor() notifies.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

namespace ble_svc
{

extern const bt_uuid_128 tindeq_service_uuid;

/** @brief The data point's value attribute. */
[[nodiscard]] const bt_gatt_attr &tindeq_data_point() noexcept;

} // namespace ble_svc
