/**
 * @file motion_sensor.hpp
 * @brief hal::IMotionSensor: the capability of noticing that the device moved.
 *
 * No Zephyr includes: logic that depends on this stays independent of the
 * hardware.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fw/error.hpp>

namespace hal
{

class IMotionSensor
{
public:
    /** @brief Start detecting motion. Fails with no_such_device if the sensor is not usable. */
    [[nodiscard]] virtual fw::Result<> init() = 0;

    /** @brief Whether the device moved since init() or the last call. Thread-safe. */
    [[nodiscard]] virtual bool moved() = 0;

protected:
    // Never deleted through the interface: objects are static.
    ~IMotionSensor() = default;
};

} // namespace hal
