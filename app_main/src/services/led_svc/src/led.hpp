/**
 * @file led.hpp
 * @brief led_svc::Led: an RGB LED over three injected hal::IDigitalOutputs, one color at a time.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "led_svc.hpp"

#include <fw/error.hpp>
#include <hal/digital_output.hpp>

namespace led_svc
{

class Led final : public ILed
{
public:
    constexpr Led( hal::IDigitalOutput &red, hal::IDigitalOutput &green, hal::IDigitalOutput &blue ) noexcept
        : red_{ red }
        , green_{ green }
        , blue_{ blue }
    {
    }

    [[nodiscard]] fw::Result<> init() override;
    [[nodiscard]] fw::Result<> show( Color color ) override;
    [[nodiscard]] Color color() const override;

private:
    hal::IDigitalOutput &red_;
    hal::IDigitalOutput &green_;
    hal::IDigitalOutput &blue_;
    Color color_{ Color::off };
};

} // namespace led_svc
