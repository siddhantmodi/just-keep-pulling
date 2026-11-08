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

fw::Result<> Power::check()
{
    if( checked_ ) {
        since_reading_ += next_check_;
    }
    checked_ = true;
    return state_ == State::powering_off ? power_off() : check_battery();
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
    if( auto level = gauge_.update( *status, since_reading_ ); level ) {
        percent_ = *level;
    } else {
        LOG_WRN( "fuel gauge update failed: %d", fw::err::to_zephyr( level.error() ) );
    }
    since_reading_ = std::chrono::milliseconds{ 0 };
    // Parsed by scripts/battery_log.py.
    LOG_DBG( "battery %u %%, %u mV, %d uA, %d mC, charge %u, usb %u", percent_, static_cast<unsigned>( status->millivolts ),
             static_cast<int>( status->microamps ), static_cast<int>( status->die_millicelsius ), static_cast<unsigned>( status->charge ),
             status->external_power ? 1U : 0U );
    if( auto ret = channels::battery.publish( { .status = *status, .percent = percent_ }, channels::lock_timeout ); !ret ) {
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
    if( auto ret = client_.send_progressor( warning.bytes() ); !ret && ret.error() != std::errc::not_connected ) {
        LOG_WRN( "low-power warning not sent: %d", fw::err::to_zephyr( ret.error() ) );
    }
    state_ = State::powering_off;
    next_check_ = warning_delay;
    return {};
}

fw::Result<> Power::power_off()
{
    if( auto saved = gauge_.save(); !saved ) {
        LOG_WRN( "fuel gauge state not saved: %d", fw::err::to_zephyr( saved.error() ) );
    }
    auto ret = pmic_.ship_mode();
    // Still running: ship mode failed, or this board stays powered (native_sim).
    monitor();
    return ret;
}

} // namespace power_manager
