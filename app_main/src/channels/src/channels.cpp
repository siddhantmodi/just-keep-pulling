/**
 * @file channels.cpp
 * @brief Definitions of app_main's zbus channels.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "channels.hpp"

#include <zephyr/zbus/zbus.h>

// Commands are events, never read back, so the initial value is only a placeholder.
ZBUS_CHAN_DEFINE( progressor_command_chan, progressor::Command, nullptr, nullptr, ZBUS_OBSERVERS_EMPTY,
                  ZBUS_MSG_INIT( progressor::Opcode::stop_measurement, 0.0F ) );
ZBUS_CHAN_DEFINE( client_connection_chan, channels::ClientConnection, nullptr, nullptr, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT( false ) );
ZBUS_CHAN_DEFINE( battery_chan, hal::BatteryStatus, nullptr, nullptr, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT( 0, false, false ) );
