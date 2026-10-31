/**
 * @file channels.cpp
 * @brief Definitions of app_template's zbus channels.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "channels.hpp"

#include <zephyr/zbus/zbus.h>

ZBUS_CHAN_DEFINE( led_request_chan, channels::LedRequest, nullptr, nullptr, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT( false ) );
ZBUS_CHAN_DEFINE( led_state_chan, channels::LedState, nullptr, nullptr, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT( false ) );
