/**
 * @file motion.hpp
 * @brief motion_svc::Motion: motion detection and wake-on-motion over an injected
 *        hal::IMotionSensor and hal::IPowerOff (System OFF, woken by the sensor's interrupt).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "motion_svc.hpp"

#include <fw/error.hpp>
#include <hal/motion_sensor.hpp>
#include <hal/power_off.hpp>

namespace motion_svc
{

class Motion final : public IMotion
{
public:
    constexpr Motion( hal::IMotionSensor &sensor, hal::IPowerOff &system_off ) noexcept
        : sensor_{ sensor }
        , system_off_{ system_off }
    {
    }

    [[nodiscard]] fw::Result<> init() override;
    [[nodiscard]] bool moved() override;
    [[nodiscard]] fw::Result<> sleep_until_moved() override;

private:
    hal::IMotionSensor &sensor_;
    hal::IPowerOff &system_off_;
};

} // namespace motion_svc
