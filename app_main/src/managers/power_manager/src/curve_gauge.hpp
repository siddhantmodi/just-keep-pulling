/**
 * @file curve_gauge.hpp
 * @brief power_manager::CurveGauge: a hal::IFuelGauge from the voltage alone, where Nordic's library is not built (native_sim).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fw/error.hpp>
#include <hal/fuel_gauge.hpp>
#include <span>

namespace power_manager
{

/** @brief A rough level from a LiPo cell's voltage, by linear interpolation of a typical discharge curve, with 0 % at the power-off voltage (3.3 V). */
[[nodiscard]] constexpr std::uint8_t curve_percent( std::uint32_t millivolts ) noexcept
{
    struct Point {
        std::uint32_t millivolts;
        std::uint32_t percent;
    };
    constexpr std::array curve{ Point{ 3300, 0 },  Point{ 3600, 5 },  Point{ 3700, 15 }, Point{ 3750, 25 }, Point{ 3800, 40 },
                                Point{ 3850, 55 }, Point{ 3900, 65 }, Point{ 4000, 80 }, Point{ 4100, 92 }, Point{ 4200, 100 } };

    if( millivolts <= curve.front().millivolts ) {
        return 0;
    }
    for( std::size_t i = 1; i < curve.size(); ++i ) {
        if( millivolts <= curve[i].millivolts ) {
            Point const low = curve[i - 1];
            Point const high = curve[i];
            return static_cast<std::uint8_t>( low.percent + ( ( millivolts - low.millivolts ) * ( high.percent - low.percent ) ) /
                                                                ( high.millivolts - low.millivolts ) );
        }
    }
    return 100;
}

class CurveGauge final : public hal::IFuelGauge
{
public:
    [[nodiscard]] fw::Result<> init( const hal::BatteryStatus & /*status*/, std::span<const std::byte> /*saved_state*/ ) override
    {
        return {};
    }

    [[nodiscard]] fw::Result<float> update( const hal::BatteryStatus &status, std::chrono::milliseconds /*elapsed*/ ) override
    {
        return static_cast<float>( curve_percent( status.millivolts ) );
    }

    [[nodiscard]] std::size_t state_size() const override
    {
        return 0;
    }

    [[nodiscard]] fw::Result<> save_state( std::span<std::byte> /*out*/ ) override
    {
        return {};
    }
};

} // namespace power_manager
