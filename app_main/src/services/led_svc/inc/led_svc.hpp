/**
 * @file led_svc.hpp
 * @brief LED service public API: the ILed interface and the app's RGB status LED.
 *
 * Not thread-safe: call from a single thread (the power manager).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <fw/error.hpp>

namespace led_svc
{

enum class Color : std::uint8_t {
    off,
    red,
    green,
    blue,
};

class ILed
{
public:
    /** @brief Configure the LED, off. Fails with no_such_device if its pins are not usable. */
    [[nodiscard]] virtual fw::Result<> init() = 0;

    /** @brief Show @p color, or nothing. */
    [[nodiscard]] virtual fw::Result<> show( Color color ) = 0;

    [[nodiscard]] virtual Color color() const = 0;

protected:
    // Never deleted through the interface: objects are static.
    ~ILed() = default;
};

/** @brief The app's status LED (devicetree aliases status-led-red, -green and -blue). */
[[nodiscard]] ILed &led();

} // namespace led_svc
