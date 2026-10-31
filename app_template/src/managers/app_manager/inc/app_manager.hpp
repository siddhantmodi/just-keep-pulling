/**
 * @file app_manager.hpp
 * @brief App manager public API.
 *
 * The app manager thread is the only caller of the single-threaded services
 * (led_svc). Other components talk to it over zbus (see channels.hpp).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fw/error.hpp>

namespace app_manager
{

/** @brief Publish the initial state and start the manager thread. Call once services are initialized. */
[[nodiscard]] fw::Result<> start();

} // namespace app_manager
