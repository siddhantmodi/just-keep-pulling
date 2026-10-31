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

#include <fw/channel_ref.hpp>
#include <hal/battery_charger.hpp>

namespace channels
{

/** @brief Whether a client (e.g. the Tindeq app) is connected over Bluetooth. */
struct ClientConnection {
    bool connected;
};

} // namespace channels

ZBUS_CHAN_DECLARE( progressor_command_chan, client_connection_chan, battery_chan );

namespace channels
{

/** @brief Commands the client wrote to the Progressor control point, decoded. */
inline constexpr fw::ChannelRef<progressor::Command> progressor_command{ progressor_command_chan };
inline constexpr fw::ChannelRef<ClientConnection> client_connection{ client_connection_chan };
/** @brief The latest battery reading, published by the power manager at boot and then periodically. */
inline constexpr fw::ChannelRef<hal::BatteryStatus> battery{ battery_chan };

} // namespace channels
