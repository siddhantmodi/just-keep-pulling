/**
 * @file led.hpp
 * @brief led_svc::Led: an RGB LED as three Zephyr LEDs, one color at a time.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "led_svc.hpp"

#include <zephyr/drivers/led.h>

#include <fw/error.hpp>

namespace led_svc
{

class Led final : public ILed
{
public:
    constexpr Led( const led_dt_spec &red, const led_dt_spec &green, const led_dt_spec &blue ) noexcept
        : red_{ red }
        , green_{ green }
        , blue_{ blue }
    {
    }

    [[nodiscard]] fw::Result<> init() override;
    [[nodiscard]] fw::Result<> show( Color color ) override;
    [[nodiscard]] Color color() const override;

private:
    led_dt_spec red_;
    led_dt_spec green_;
    led_dt_spec blue_;
    Color color_{ Color::off };
};

} // namespace led_svc
