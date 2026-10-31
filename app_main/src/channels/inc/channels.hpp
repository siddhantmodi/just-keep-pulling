/**
 * @file channels.hpp
 * @brief app_main's zbus channels: the message types components exchange and
 *        a typed handle for each channel.
 *
 * Channels know nothing about their observers; each observer attaches itself
 * with ZBUS_CHAN_ADD_OBS next to its own code.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "progressor.hpp"

#include <zephyr/zbus/zbus.h>

#include <cstdint>
#include <fw/channel_ref.hpp>
#include <hal/battery_charger.hpp>

namespace channels
{

/** @brief Whether a client (e.g. the Tindeq app) is connected over Bluetooth. */
struct ClientConnection {
    bool connected;
};

/** @brief Whether a measurement is running (the load cell is sampling for the client). */
struct MeasurementState {
    bool measuring;
};

/** @brief A configuration request from the client (the JustKeepPulling service), already validated. */
struct ConfigCommand {
    enum class Type : std::uint8_t {
        add_calibration_point,
        reset_calibration,
        set_sample_rate,
    };

    Type type;
    float calibration_kg;             // add_calibration_point
    std::uint16_t samples_per_second; // set_sample_rate: one of hal::all_sample_rates
};

/** @brief How the last calibration request went. The values are on the wire (JustKeepPulling service). */
enum class CalibrationStatus : std::uint8_t {
    ok = 0,
    measuring = 1,    // a point is being measured (1.25 s)
    full = 2,         // 20 points already; reset to start over
    fit_rejected = 3, // the points gave an implausible factor: cleared, the factor kept
    failed = 4,       // the load cell or saving the factor failed
};

/** @brief The calibration as the measurement manager last left it. */
struct CalibrationState {
    CalibrationStatus status;
    std::uint8_t points;     // in RAM, since boot or the last reset
    float factor_g_per_code; // grams per ADC code
};

} // namespace channels

ZBUS_CHAN_DECLARE( progressor_command_chan, client_connection_chan, measurement_state_chan, battery_chan, config_command_chan,
                   calibration_state_chan );

namespace channels
{

/** @brief Commands the client wrote to the Progressor control point, decoded. */
inline constexpr fw::ChannelRef<progressor::Command> progressor_command{ progressor_command_chan };
inline constexpr fw::ChannelRef<ClientConnection> client_connection{ client_connection_chan };
/** @brief Published by the measurement manager when a measurement starts or stops. */
inline constexpr fw::ChannelRef<MeasurementState> measurement_state{ measurement_state_chan };
/** @brief The latest battery reading, published by the power manager at boot and then periodically. */
inline constexpr fw::ChannelRef<hal::BatteryStatus> battery{ battery_chan };
/** @brief Configuration requests for the measurement manager, which owns the load cell. */
inline constexpr fw::ChannelRef<ConfigCommand> config_command{ config_command_chan };
/** @brief Published by the measurement manager at start and after each calibration request. */
inline constexpr fw::ChannelRef<CalibrationState> calibration_state{ calibration_state_chan };

} // namespace channels
