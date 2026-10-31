/**
 * @file ads1220.hpp
 * @brief hal::zephyr::Ads1220: hal::ILoadCellAdc over this repo's ADS1220 driver (<jkp/drivers/ads1220.h>).
 *
 * Constructed from the devicetree node's device, so the app decides which ADC
 * is used:
 * @code
 * constinit hal::zephyr::Ads1220 adc{ DEVICE_DT_GET( DT_ALIAS( load_cell_adc ) ) };
 * @endcode
 *
 * Not thread-safe: use from one thread.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include <chrono>
#include <cstdint>
#include <expected>
#include <fw/error.hpp>
#include <hal/load_cell_adc.hpp>
#include <jkp/drivers/ads1220.h>
#include <system_error>

namespace hal::zephyr
{

class Ads1220 final : public ILoadCellAdc
{
public:
    constexpr explicit Ads1220( const device *dev ) noexcept
        : dev_{ dev }
    {
    }

    [[nodiscard]] fw::Result<> init() override
    {
        if( !device_is_ready( dev_ ) ) {
            return std::unexpected( std::errc::no_such_device );
        }
        return {};
    }

    [[nodiscard]] bool supports_rate( std::uint16_t samples_per_second ) const override
    {
        return ads1220_supports_rate( samples_per_second );
    }

    [[nodiscard]] fw::Result<> start( std::uint16_t samples_per_second ) override
    {
        return fw::err::check( ads1220_start( dev_, samples_per_second ) );
    }

    [[nodiscard]] fw::Result<LoadCellSample> read( std::chrono::microseconds timeout ) override
    {
        ads1220_sample sample{};
        if( auto ret = fw::err::check( ads1220_read( dev_, &sample, K_USEC( timeout.count() ) ) ); !ret ) {
            return std::unexpected( ret.error() );
        }
        return LoadCellSample{ .raw = sample.raw, .timestamp_us = sample.ready_us };
    }

    [[nodiscard]] std::uint32_t lost_samples() const override
    {
        return ads1220_lost_samples( dev_ );
    }

    [[nodiscard]] fw::Result<> stop() override
    {
        return fw::err::check( ads1220_stop( dev_ ) );
    }

private:
    const device *dev_;
};

} // namespace hal::zephyr
