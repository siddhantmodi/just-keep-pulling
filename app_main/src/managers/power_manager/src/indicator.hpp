/**
 * @file indicator.hpp
 * @brief power_manager::Indicator: the status LED's logic, as Crimpdeq's.
 *
 * Blue while waiting for a client, green while one is connected, red
 * blinking (250 ms on, 750 ms off) while the battery is at or below 3.5 V,
 * and off while the device sleeps. While blinking, the manager thread calls
 * on_blink_step() every blink_step.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "led_svc.hpp"

#include <chrono>
#include <cstdint>
#include <fw/error.hpp>

namespace power_manager
{

inline constexpr std::uint32_t low_battery_led_mv = 3500;
inline constexpr std::chrono::milliseconds blink_step{ 250 };
/** @brief Steps per blink period: one on, the rest off. */
inline constexpr unsigned blink_steps = 4;

class Indicator
{
public:
    constexpr explicit Indicator( led_svc::ILed &led ) noexcept
        : led_{ led }
    {
    }

    /** @brief Show the initial state: no client yet. */
    [[nodiscard]] fw::Result<> start();
    [[nodiscard]] fw::Result<> on_client_connection( bool connected );
    [[nodiscard]] fw::Result<> on_battery( std::uint32_t millivolts );
    [[nodiscard]] fw::Result<> on_sleep( bool sleeping );
    [[nodiscard]] fw::Result<> on_blink_step();

    /** @brief Whether on_blink_step() should be called. */
    [[nodiscard]] bool blinking() const noexcept
    {
        return low_battery_ && !sleeping_;
    }

private:
    fw::Result<> show();

    led_svc::ILed &led_;
    bool connected_{ false };
    bool low_battery_{ false };
    bool sleeping_{ false };
    unsigned step_{ 0 };
};

} // namespace power_manager
