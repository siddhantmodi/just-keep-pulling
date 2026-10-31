/**
 * @file temperature_sensor.hpp
 * @brief hal::ITemperatureSensor: the capability of measuring a temperature.
 *
 * No Zephyr includes: logic that depends on this stays independent of the
 * hardware.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <fw/error.hpp>

namespace hal
{

class ITemperatureSensor
{
public:
    /** @brief Check the sensor is usable. Fails with no_such_device if it is not ready. */
    [[nodiscard]] virtual fw::Result<> init() = 0;

    /** @brief Take a measurement, in milli-degrees Celsius. */
    [[nodiscard]] virtual fw::Result<std::int32_t> read_millicelsius() = 0;

protected:
    // Never deleted through the interface: objects are static.
    ~ITemperatureSensor() = default;
};

} // namespace hal
