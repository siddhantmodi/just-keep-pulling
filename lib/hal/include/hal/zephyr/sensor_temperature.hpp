/**
 * @file sensor_temperature.hpp
 * @brief hal::zephyr::SensorTemperature: hal::ITemperatureSensor over one channel of a Zephyr sensor.
 *
 * Constructed from a device and channel, so the app (not the HAL) decides
 * which sensor is used:
 * @code
 * constinit hal::zephyr::SensorTemperature die{ DEVICE_DT_GET( DT_ALIAS( die_temp ) ), SENSOR_CHAN_DIE_TEMP };
 * @endcode
 *
 * Not thread-safe: a read is a sample fetch followed by a channel get, and the
 * driver holds the sample in between. Serialize reads from multiple threads.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

#include <cstdint>
#include <expected>
#include <fw/error.hpp>
#include <hal/temperature_sensor.hpp>
#include <limits>
#include <system_error>

namespace hal::zephyr
{

class SensorTemperature final : public ITemperatureSensor
{
public:
    constexpr SensorTemperature( const device *dev, sensor_channel channel ) noexcept
        : dev_{ dev }
        , channel_{ channel }
    {
    }

    [[nodiscard]] fw::Result<> init() override
    {
        if( !device_is_ready( dev_ ) ) {
            return std::unexpected( std::errc::no_such_device );
        }
        return {};
    }

    [[nodiscard]] fw::Result<std::int32_t> read_millicelsius() override
    {
        if( auto ret = fw::err::check( sensor_sample_fetch_chan( dev_, channel_ ) ); !ret ) {
            return std::unexpected( ret.error() );
        }
        sensor_value value{};
        if( auto ret = fw::err::check( sensor_channel_get( dev_, channel_, &value ) ); !ret ) {
            return std::unexpected( ret.error() );
        }
        std::int64_t const millicelsius = sensor_value_to_milli( &value );
        if( millicelsius < std::numeric_limits<std::int32_t>::min() || millicelsius > std::numeric_limits<std::int32_t>::max() ) {
            return std::unexpected( std::errc::result_out_of_range );
        }
        return static_cast<std::int32_t>( millicelsius );
    }

private:
    const device *dev_;
    sensor_channel channel_;
};

} // namespace hal::zephyr
