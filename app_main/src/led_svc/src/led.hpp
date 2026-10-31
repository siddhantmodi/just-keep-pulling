/**
 * @file led.hpp
 * @brief led_svc::Led: LED state logic over an injected hal::IDigitalOutput.
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
    constexpr explicit Led( hal::IDigitalOutput &output ) noexcept
        : output_{ output }
    {
    }

    [[nodiscard]] fw::Result<> init() override;
    [[nodiscard]] fw::Result<> set( bool on ) override;
    [[nodiscard]] bool is_on() const override;

private:
    hal::IDigitalOutput &output_;
    bool initialized_{ false };
    bool on_{ false };
};

} // namespace led_svc
