/**
 * @file activity.cpp
 * @brief power_manager::Activity implementation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "activity.hpp"

#include <zephyr/logging/log.h>

#include <system_error>

LOG_MODULE_DECLARE( power_manager, CONFIG_APP_POWER_MANAGER_LOG_LEVEL );

namespace power_manager
{

void Activity::reset()
{
    idle_ = std::chrono::seconds{ 0 };
    disconnecting_ = false;
}

void Activity::on_command( progressor::Opcode opcode )
{
    // The app polls the battery while it sits open; that alone shouldn't keep the device awake.
    if( opcode != progressor::Opcode::sample_battery ) {
        reset();
    }
}

void Activity::on_client_connection( bool connected )
{
    connected_ = connected;
    // The disconnection we asked for: go on to sleep.
    if( !connected && disconnecting_ ) {
        return;
    }
    reset();
}

void Activity::on_measurement( bool measuring )
{
    measuring_ = measuring;
    reset();
}

fw::Result<> Activity::check()
{
    if( motion_.moved() || measuring_ ) {
        reset();
        return {};
    }
    if( disconnecting_ ) {
        return sleep();
    }

    idle_ += activity_check_interval;
    if( idle_ < ( connected_ ? timeouts_.connected : timeouts_.disconnected ) ) {
        return {};
    }

    LOG_INF( "idle for %lld s", static_cast<long long>( idle_.count() ) );
    if( !connected_ ) {
        return sleep();
    }
    // As Crimpdeq: the client sees the link close rather than time out.
    if( auto ret = client_.disconnect(); !ret && ret.error() != std::errc::not_connected ) {
        LOG_WRN( "disconnect failed: %d", fw::err::to_zephyr( ret.error() ) );
    }
    disconnecting_ = true;
    return {};
}

fw::Result<> Activity::sleep()
{
    // GPIOs keep their state in System OFF: an LED left on would stay on.
    if( auto ret = indicator_.on_sleep( true ); !ret ) {
        LOG_WRN( "LED not turned off: %d", fw::err::to_zephyr( ret.error() ) );
    }
    auto ret = motion_.sleep_until_moved();
    // Still running: sleeping failed, or this board can't (native_sim). Start counting again.
    if( auto shown = indicator_.on_sleep( false ); !shown ) {
        LOG_WRN( "LED not restored: %d", fw::err::to_zephyr( shown.error() ) );
    }
    reset();
    return ret;
}

} // namespace power_manager
