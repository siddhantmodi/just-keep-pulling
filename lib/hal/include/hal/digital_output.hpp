/**
 * @file digital_output.hpp
 * @brief hal::IDigitalOutput: the capability of driving a single output pin.
 *
 * No Zephyr includes: logic that depends on this stays independent of the
 * hardware. Levels are logical ("active"), so active-low wiring is handled by
 * the implementation (devicetree flags), not by the caller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fw/error.hpp>

namespace hal
{

class IDigitalOutput
{
public:
    /** @brief Configure as an output driven to @p active. Fails with no_such_device if the controller is not ready. */
    [[nodiscard]] virtual fw::Result<> init( bool active ) = 0;

    [[nodiscard]] virtual fw::Result<> set( bool active ) = 0;

protected:
    // Never deleted through the interface: objects are static.
    ~IDigitalOutput() = default;
};

} // namespace hal
