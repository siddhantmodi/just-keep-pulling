/**
 * @file led_svc.hpp
 * @brief LED service public API: the ILed interface and the app's LED.
 *
 * Not thread-safe: call from a single thread (the app manager).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fw/error.hpp>

namespace led_svc
{

class ILed
{
public:
    /** @brief Configure the LED and turn it off. */
    [[nodiscard]] virtual fw::Result<> init() = 0;

    /** @brief Turn the LED on or off. Fails with operation_not_permitted before init(). */
    [[nodiscard]] virtual fw::Result<> set( bool on ) = 0;

    /** @brief Last state successfully applied to the LED. */
    [[nodiscard]] virtual bool is_on() const = 0;

protected:
    // Never deleted through the interface: objects are static.
    ~ILed() = default;
};

/** @brief The app's LED (devicetree alias led0). */
[[nodiscard]] ILed &led();

} // namespace led_svc
