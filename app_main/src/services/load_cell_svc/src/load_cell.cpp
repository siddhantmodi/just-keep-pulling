/**
 * @file load_cell.cpp
 * @brief load_cell_svc::LoadCell implementation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "load_cell.hpp"

#include <zephyr/logging/log.h>

#include <algorithm>
#include <cmath>
#include <expected>
#include <fw/settings.hpp>
#include <system_error>

LOG_MODULE_REGISTER( load_cell_svc, CONFIG_APP_LOAD_CELL_SVC_LOG_LEVEL );

namespace load_cell_svc
{

namespace
{

/** @brief Long enough for the next conversion: two periods plus a margin for scheduling. */
std::chrono::microseconds read_timeout( std::uint16_t samples_per_second )
{
    return std::chrono::microseconds{ ( 2'000'000 / samples_per_second ) + 20'000 };
}

} // namespace

fw::Result<> LoadCell::init()
{
    if( auto ret = adc_.init(); !ret ) {
        LOG_ERR( "ADC not ready: %d", fw::err::to_zephyr( ret.error() ) );
        return ret;
    }

    auto stored = fw::settings::load<float>( factor_key );
    if( !stored ) {
        if( stored.error() != std::errc::no_such_file_or_directory ) {
            LOG_WRN( "calibration factor unreadable (%d), using the default", fw::err::to_zephyr( stored.error() ) );
        }
        factor_ = calibration::default_factor;
    } else if( !calibration::is_plausible( *stored ) ) {
        LOG_WRN( "saved calibration factor implausible, using the default" );
        factor_ = calibration::default_factor;
    } else {
        factor_ = *stored;
    }

    auto rate = fw::settings::load<std::uint16_t>( rate_key );
    if( rate && adc_.supports_rate( *rate ) ) {
        configured_rate_ = *rate;
    } else {
        if( rate || rate.error() != std::errc::no_such_file_or_directory ) {
            LOG_WRN( "saved sample rate unusable, using %u SPS", default_samples_per_second );
        }
        configured_rate_ = default_samples_per_second;
    }
    return {};
}

std::uint16_t LoadCell::sample_rate() const
{
    return configured_rate_;
}

fw::Result<> LoadCell::set_sample_rate( std::uint16_t samples_per_second )
{
    if( !adc_.supports_rate( samples_per_second ) ) {
        return std::unexpected( std::errc::not_supported );
    }
    if( auto ret = fw::settings::save( rate_key, samples_per_second ); !ret ) {
        return ret;
    }
    configured_rate_ = samples_per_second;
    LOG_INF( "sample rate set to %u SPS", samples_per_second );
    return {};
}

fw::Result<> LoadCell::start( std::uint16_t samples_per_second )
{
    if( auto ret = adc_.start( samples_per_second ); !ret ) {
        return ret;
    }
    running_rate_ = samples_per_second;
    return {};
}

fw::Result<ForceSample> LoadCell::read( std::chrono::microseconds timeout )
{
    auto sample = adc_.read( timeout );
    if( !sample ) {
        return std::unexpected( sample.error() );
    }
    return ForceSample{ .kg = calibration::to_kg( sample->raw, tare_, factor_ ), .timestamp_us = sample->timestamp_us };
}

fw::Result<> LoadCell::stop()
{
    running_rate_ = 0;
    return adc_.stop();
}

std::uint32_t LoadCell::lost_samples() const
{
    return adc_.lost_samples();
}

fw::Result<double> LoadCell::average_raw( std::uint16_t samples_per_second, std::chrono::milliseconds duration )
{
    std::uint16_t const previous_rate = running_rate_;
    if( previous_rate != samples_per_second ) {
        if( auto ret = adc_.start( samples_per_second ); !ret ) {
            return std::unexpected( ret.error() );
        }
    }

    auto const count = std::max<std::int64_t>( 1, ( std::int64_t{ samples_per_second } * duration.count() ) / 1000 );
    auto const timeout = read_timeout( samples_per_second );
    std::int64_t sum = 0;
    fw::Result<> result{};
    for( std::int64_t i = 0; i < count && result; ++i ) {
        auto sample = adc_.read( timeout );
        if( sample ) {
            sum += sample->raw;
        } else {
            result = std::unexpected( sample.error() );
        }
    }

    // Restore the previous sampling: stopped, or running at its own rate.
    fw::Result<> restored{};
    if( previous_rate == 0 ) {
        restored = adc_.stop();
    } else if( previous_rate != samples_per_second ) {
        restored = adc_.start( previous_rate );
    }
    if( !result ) {
        return std::unexpected( result.error() );
    }
    if( !restored ) {
        running_rate_ = 0;
        return std::unexpected( restored.error() );
    }
    return static_cast<double>( sum ) / static_cast<double>( count );
}

fw::Result<> LoadCell::tare()
{
    std::uint16_t const rate = running_rate_ != 0 ? running_rate_ : default_samples_per_second;
    auto average = average_raw( rate, tare_duration );
    if( !average ) {
        LOG_ERR( "tare failed: %d", fw::err::to_zephyr( average.error() ) );
        return std::unexpected( average.error() );
    }
    tare_ = static_cast<std::int32_t>( std::lround( *average ) );
    LOG_INF( "tared at %d", tare_ );
    return {};
}

fw::Result<std::size_t> LoadCell::add_calibration_point( float kg )
{
    if( !std::isfinite( kg ) || kg < 0.0F ) {
        return std::unexpected( std::errc::invalid_argument );
    }
    if( point_count_ == points_.size() ) {
        return std::unexpected( std::errc::no_buffer_space );
    }

    auto average = average_raw( calibration_samples_per_second, calibration_duration );
    if( !average ) {
        LOG_ERR( "calibration point failed: %d", fw::err::to_zephyr( average.error() ) );
        return std::unexpected( average.error() );
    }
    points_[point_count_++] = calibration::Point{ .raw = *average, .kg = kg };
    LOG_INF( "calibration point %u: %d codes at %d g", static_cast<unsigned>( point_count_ ), static_cast<int>( *average ),
             static_cast<int>( kg * 1000.0F ) );
    if( point_count_ < 2 ) {
        return point_count_;
    }

    auto factor = calibration::fit_factor( std::span{ points_ }.first( point_count_ ) );
    if( !factor ) {
        LOG_ERR( "calibration fit failed (%d), points cleared", fw::err::to_zephyr( factor.error() ) );
        point_count_ = 0;
        return std::unexpected( factor.error() );
    }
    factor_ = *factor;
    if( auto ret = fw::settings::save( factor_key, factor_ ); !ret ) {
        LOG_ERR( "calibration factor not saved: %d", fw::err::to_zephyr( ret.error() ) );
        return std::unexpected( ret.error() );
    }
    return point_count_;
}

fw::Result<> LoadCell::reset_calibration()
{
    point_count_ = 0;
    factor_ = calibration::default_factor;
    return fw::settings::erase( factor_key );
}

float LoadCell::calibration_factor() const
{
    return factor_;
}

} // namespace load_cell_svc
