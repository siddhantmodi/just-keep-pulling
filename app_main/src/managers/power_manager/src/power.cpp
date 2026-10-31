/**
 * @file power.cpp
 * @brief power_manager::Power implementation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "power.hpp"

#include "channels.hpp"
#include "progressor.hpp"

#include <zephyr/logging/log.h>

#include <expected>
#include <system_error>

LOG_MODULE_REGISTER( power_manager, CONFIG_APP_POWER_MANAGER_LOG_LEVEL );

namespace power_manager
{

fw::Result<> Power::handle( const Event &event )
{
    switch( event.type ) {
    case Event::Type::battery_check:
        return state_ == State::powering_off ? power_off() : check_battery();
    }
    return std::unexpected( std::errc::invalid_argument );
}

void Power::monitor()
{
    state_ = State::monitoring;
    next_check_ = check_interval;
}

fw::Result<> Power::check_battery()
{
    auto status = pmic_.read_battery();
    if( !status ) {
        monitor();
        return std::unexpected( status.error() );
    }
    if( auto ret = channels::battery.publish( *status ); !ret ) {
        LOG_ERR( "battery publish failed: %d", fw::err::to_zephyr( ret.error() ) );
    }

    if( !is_low( *status ) ) {
        if( state_ == State::confirming_low ) {
            LOG_INF( "battery recovered: %u mV", static_cast<unsigned>( status->millivolts ) );
        }
        monitor();
        return {};
    }
    if( state_ == State::monitoring ) {
        LOG_WRN( "battery low: %u mV, confirming", static_cast<unsigned>( status->millivolts ) );
        state_ = State::confirming_low;
        next_check_ = confirm_delay;
        return {};
    }

    LOG_WRN( "battery low: %u mV, powering off", static_cast<unsigned>( status->millivolts ) );
    auto const warning = progressor::encode_low_power_warning();
    if( auto ret = client_.send( warning.bytes() ); !ret && ret.error() != std::errc::not_connected ) {
        LOG_WRN( "low-power warning not sent: %d", fw::err::to_zephyr( ret.error() ) );
    }
    state_ = State::powering_off;
    next_check_ = warning_delay;
    return {};
}

fw::Result<> Power::power_off()
{
    auto ret = pmic_.ship_mode();
    // Still running: ship mode failed, or this board stays powered (native_sim).
    monitor();
    return ret;
}

} // namespace power_manager
