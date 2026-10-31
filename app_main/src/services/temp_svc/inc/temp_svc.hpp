/**
 * @file temp_svc.hpp
 * @brief Temperature service public API: the ITemperature interface and the app's die temperature.
 *
 * read_centicelsius() is thread-safe (reads are serialized), so it can be
 * called directly from a Bluetooth callback. Call init() once at startup.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <fw/error.hpp>

namespace temp_svc
{

class ITemperature
{
public:
    /** @brief Check the sensor is ready. */
    [[nodiscard]] virtual fw::Result<> init() = 0;

    /**
     * @brief Measure the temperature in hundredths of a degree Celsius (2537 = 25.37 °C).
     *        Fails with operation_not_permitted before init().
     */
    [[nodiscard]] virtual fw::Result<std::int16_t> read_centicelsius() = 0;

protected:
    // Never deleted through the interface: objects are static.
    ~ITemperature() = default;
};

/** @brief The app's die temperature (devicetree alias die-temp). */
[[nodiscard]] ITemperature &temperature();

} // namespace temp_svc
