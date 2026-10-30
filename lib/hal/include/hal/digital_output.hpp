/**
 * @file digital_output.hpp
 * @brief hal::DigitalOutput: the capability of driving a single output pin.
 *
 * No Zephyr includes: logic that depends on this stays independent of the
 * hardware. Levels are logical ("active"), so active-low wiring is handled by
 * the implementation (devicetree flags), not by the caller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <concepts>
#include <fw/error.hpp>

namespace hal
{

template<typename T>
concept DigitalOutput = requires( T &output, bool active ) {
    /** Configure as an output driven to @p active. Fails with no_such_device if the controller is not ready. */
    { output.init( active ) } -> std::same_as<fw::Result<>>;
    { output.set( active ) } -> std::same_as<fw::Result<>>;
};

} // namespace hal
