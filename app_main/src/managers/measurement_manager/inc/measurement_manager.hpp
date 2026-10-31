/**
 * @file measurement_manager.hpp
 * @brief Measurement manager public API.
 *
 * The measurement manager thread answers the client's Progressor commands
 * (channels::progressor_command) and, while measuring, streams load-cell
 * samples to it. It also handles configuration requests (channels::config_command:
 * calibration, sample rate) and publishes channels::calibration_state. It is the
 * only caller of load_cell_svc's single-threaded operations.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fw/error.hpp>

namespace measurement_manager
{

/**
 * @brief Zero the scale and start the manager thread. Call once the load cell
 *        and identity are initialized, and before Bluetooth, so no command is missed.
 */
[[nodiscard]] fw::Result<> start();

} // namespace measurement_manager
