/**
 * @file power_off.hpp
 * @brief hal::IPowerOff: the capability of switching the whole device off.
 *
 * No Zephyr includes: logic that depends on this stays independent of the
 * hardware.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fw/error.hpp>

namespace hal
{

class IPowerOff
{
public:
    /** @brief Check the power switch is usable. Fails with no_such_device if it is not ready. */
    [[nodiscard]] virtual fw::Result<> init() = 0;

    /**
     * @brief Cut the device's power. On success, power goes within
     *        milliseconds; nothing after the call should matter. How it comes
     *        back depends on the hardware (e.g. USB or a power cycle).
     */
    [[nodiscard]] virtual fw::Result<> power_off() = 0;

protected:
    // Never deleted through the interface: objects are static.
    ~IPowerOff() = default;
};

} // namespace hal
