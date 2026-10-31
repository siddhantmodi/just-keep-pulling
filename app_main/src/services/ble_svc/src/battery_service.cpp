/**
 * @file battery_service.cpp
 * @brief Feeds Zephyr's standard Battery service, for generic apps (e.g. nRF
 *        Connect): its level is estimated from channels::battery. The
 *        Progressor protocol has its own battery command.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "battery_level.hpp"
#include "channels.hpp"

#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <atomic>
#include <cerrno>
#include <cstdint>

LOG_MODULE_DECLARE( ble_svc, CONFIG_APP_BLE_SVC_LOG_LEVEL );

namespace
{

constinit std::atomic<std::uint8_t> battery_level{ 100 };

void update_battery_service( k_work * /*work*/ )
{
    // On the system work queue, where a notification never waits for a buffer.
    if( int const err = bt_bas_set_battery_level( battery_level.load( std::memory_order_relaxed ) ); err != 0 && err != -ENOTCONN ) {
        LOG_DBG( "battery level not updated: %d", err );
    }
}

/** @brief zbus listener on channels::battery: runs in the publisher's thread, so it only hands the level on. */
void on_battery( const zbus_channel *chan );

} // namespace

K_WORK_DEFINE( ble_svc_battery_work, update_battery_service );
ZBUS_LISTENER_DEFINE( ble_svc_battery_listener, on_battery );
ZBUS_CHAN_ADD_OBS( battery_chan, ble_svc_battery_listener, 0 );

namespace
{

void on_battery( const zbus_channel * /*chan*/ )
{
    battery_level.store( ble_svc::battery_level_percent( channels::battery.const_msg().millivolts ), std::memory_order_relaxed );
    k_work_submit( &ble_svc_battery_work );
}

} // namespace
