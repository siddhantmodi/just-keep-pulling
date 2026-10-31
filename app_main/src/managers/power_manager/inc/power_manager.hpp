/**
 * @file power_manager.hpp
 * @brief Power manager public API.
 *
 * The power manager thread reads the battery every 30 s and publishes it on
 * channels::battery. A low battery (3.3 V or less, not charging and no USB
 * power, confirmed by a second reading) is announced to the client with the
 * Progressor low-power warning, then the PMIC enters ship mode, as Crimpdeq
 * sleeps. It is the only caller of pmic_svc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fw/error.hpp>

namespace power_manager
{

/**
 * @brief Read and publish the battery, then start the manager thread. Call
 *        once the PMIC is initialized, and before Bluetooth, so the first
 *        battery request is answered with a reading.
 */
[[nodiscard]] fw::Result<> start();

} // namespace power_manager
