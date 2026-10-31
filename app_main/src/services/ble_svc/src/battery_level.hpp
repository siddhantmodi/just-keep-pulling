/**
 * @file battery_level.hpp
 * @brief The battery level (0-100 %) reported by the Battery service, estimated from the voltage.
 *
 * Pure functions with no Zephyr dependency, so they are unit tested directly.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <array>
#include <cstdint>

namespace ble_svc
{

/**
 * @brief A rough level from a LiPo cell's voltage, by linear interpolation of a
 *        typical discharge curve, with 0 % where the device powers off (3.3 V).
 *        No fuel gauge: under load the voltage, and so the level, reads low.
 */
[[nodiscard]] constexpr std::uint8_t battery_level_percent( std::uint32_t millivolts ) noexcept
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

} // namespace ble_svc
