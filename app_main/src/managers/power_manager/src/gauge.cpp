/**
 * @file gauge.cpp
 * @brief power_manager::Gauge implementation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "gauge.hpp"

#include <zephyr/logging/log.h>

#include <algorithm>
#include <cmath>
#include <expected>
#include <fw/settings.hpp>
#include <system_error>

LOG_MODULE_DECLARE( power_manager, CONFIG_APP_POWER_MANAGER_LOG_LEVEL );

namespace power_manager
{

fw::Result<std::uint8_t> Gauge::update( const hal::BatteryStatus &status, std::chrono::milliseconds elapsed )
{
    if( !started_ ) {
        if( auto ret = start( status ); !ret ) {
            return std::unexpected( ret.error() );
        }
        elapsed = std::chrono::milliseconds{ 0 };
    }
    auto soc = gauge_.update( status, elapsed );
    if( !soc ) {
        return std::unexpected( soc.error() );
    }
    unsaved_ += elapsed;
    if( unsaved_ >= gauge_save_interval ) {
        if( auto ret = save(); !ret ) {
            LOG_WRN( "fuel gauge state not saved: %d", fw::err::to_zephyr( ret.error() ) );
        }
    }
    return static_cast<std::uint8_t>( std::clamp( std::lround( *soc ), 0L, 100L ) );
}

fw::Result<> Gauge::start( const hal::BatteryStatus &status )
{
    std::size_t const size = gauge_.state_size();
    std::span<const std::byte> saved{};
    if( size > state_.size() ) {
        LOG_ERR( "fuel gauge state (%u B) does not fit, not resuming it", static_cast<unsigned>( size ) );
    } else if( size > 0 ) {
        auto const buffer = std::span{ state_ }.first( size );
        if( auto loaded = fw::settings::load_bytes( gauge_state_key, buffer ); loaded ) {
            saved = buffer;
        } else if( loaded.error() != std::errc::no_such_file_or_directory ) {
            LOG_WRN( "saved fuel gauge state unreadable (%d)", fw::err::to_zephyr( loaded.error() ) );
        }
    }

    auto ret = gauge_.init( status, saved );
    if( !ret && !saved.empty() ) {
        LOG_WRN( "saved fuel gauge state unusable, starting fresh" );
        saved = {};
        ret = gauge_.init( status, saved );
    }
    if( !ret ) {
        LOG_ERR( "fuel gauge init failed: %d", fw::err::to_zephyr( ret.error() ) );
        return ret;
    }
    LOG_INF( "fuel gauge %s", saved.empty() ? "started from the voltage" : "resumed" );
    started_ = true;
    unsaved_ = std::chrono::milliseconds{ 0 };
    return {};
}

fw::Result<> Gauge::save()
{
    std::size_t const size = gauge_.state_size();
    if( !started_ || size == 0 ) {
        return {};
    }
    if( size > state_.size() ) {
        return std::unexpected( std::errc::no_buffer_space );
    }
    auto const buffer = std::span{ state_ }.first( size );
    if( auto ret = gauge_.save_state( buffer ); !ret ) {
        return ret;
    }
    if( auto ret = fw::settings::save_bytes( gauge_state_key, buffer ); !ret ) {
        return ret;
    }
    unsaved_ = std::chrono::milliseconds{ 0 };
    return {};
}

} // namespace power_manager
