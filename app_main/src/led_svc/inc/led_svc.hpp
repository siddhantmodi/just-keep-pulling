/**
 * @file led_svc.hpp
 * @brief LED service public API: drives the board's led0.
 *
 * Not thread-safe: call from a single thread (the app's manager thread).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fw/error.hpp>

namespace led_svc
{

/** @brief Configure the LED GPIO and turn the LED off. */
[[nodiscard]] fw::Result<> init();

/** @brief Turn the LED on or off. Fails with operation_not_permitted before init(). */
[[nodiscard]] fw::Result<> set( bool on );

/** @brief Invert the LED state. */
[[nodiscard]] fw::Result<> toggle();

/** @brief Last state successfully applied to the LED. */
[[nodiscard]] bool is_on();

} // namespace led_svc
