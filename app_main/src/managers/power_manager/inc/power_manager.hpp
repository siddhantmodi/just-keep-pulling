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
 * It also tracks inactivity (commands, connections, measurements and motion
 * are activity): after 4 minutes idle, or 30 with a client connected, it
 * disconnects the client and sleeps (System OFF) until the device is moved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fw/error.hpp>

namespace power_manager
{

/**
 * @brief Read and publish the battery, then start the manager thread. Call
 *        once the PMIC and the IMU are initialized, and before Bluetooth, so
 *        the first battery request is answered with a reading.
 */
[[nodiscard]] fw::Result<> start();

} // namespace power_manager
