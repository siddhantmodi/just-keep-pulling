/**
 * @file die_temperature.hpp
 * @brief temp_svc::DieTemperature: die temperature logic over an injected hal::ITemperatureSensor.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "temp_svc.hpp"

#include <zephyr/kernel.h>

#include <cstdint>
#include <expected>
#include <fw/error.hpp>
#include <hal/temperature_sensor.hpp>
#include <limits>
#include <system_error>

namespace temp_svc
{

/**
 * @brief Convert milli- to centi-degrees Celsius, rounding half away from zero.
 *        Fails with result_out_of_range if the result does not fit in an int16_t.
 */
[[nodiscard]] constexpr fw::Result<std::int16_t> to_centicelsius( std::int32_t millicelsius ) noexcept
{
    std::int64_t const milli = millicelsius;
    std::int64_t const centi = ( milli >= 0 ? milli + 5 : milli - 5 ) / 10;
    if( centi < std::numeric_limits<std::int16_t>::min() || centi > std::numeric_limits<std::int16_t>::max() ) {
        return std::unexpected( std::errc::result_out_of_range );
    }
    return static_cast<std::int16_t>( centi );
}

class DieTemperature final : public ITemperature
{
public:
    /** @param read_mutex Serializes reads: a sensor read is a fetch plus a get, and the driver holds the sample in between. */
    constexpr DieTemperature( hal::ITemperatureSensor &sensor, k_mutex &read_mutex ) noexcept
        : sensor_{ sensor }
        , read_mutex_{ read_mutex }
    {
    }

    [[nodiscard]] fw::Result<> init() override;
    [[nodiscard]] fw::Result<std::int16_t> read_centicelsius() override;

private:
    hal::ITemperatureSensor &sensor_;
    k_mutex &read_mutex_;
    bool initialized_{ false };
};

} // namespace temp_svc
