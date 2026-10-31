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

#include <zephyr/zbus/zbus.h>

#include <fw/channel_ref.hpp>

namespace channels
{

/** @brief Ask for the LED to be turned on or off (e.g. from a BLE write). */
struct LedRequest {
    bool on;
};

/** @brief The LED state last applied by the app manager. */
struct LedState {
    bool on;
};

} // namespace channels

ZBUS_CHAN_DECLARE( led_request_chan, led_state_chan );

namespace channels
{

inline constexpr fw::ChannelRef<LedRequest> led_request{ led_request_chan };
inline constexpr fw::ChannelRef<LedState> led_state{ led_state_chan };

} // namespace channels
