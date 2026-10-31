/**
 * @file battery_charger.hpp
 * @brief hal::IBatteryCharger: the capability of measuring the battery and reporting the charger's state.
 *
 * No Zephyr includes: logic that depends on this stays independent of the
 * hardware.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <fw/error.hpp>

namespace hal
{

struct BatteryStatus {
    std::uint32_t millivolts;
    /** The charger is charging the battery (trickle, constant current or constant voltage). */
    bool charging;
    /** External power (USB) is present, so the device runs whatever the battery's state. */
    bool external_power;
};

class IBatteryCharger
{
public:
    /** @brief Check the charger is usable. Fails with no_such_device if it is not ready. */
    [[nodiscard]] virtual fw::Result<> init() = 0;

    /** @brief Measure the battery and read the charger's state. */
    [[nodiscard]] virtual fw::Result<BatteryStatus> read() = 0;

protected:
    // Never deleted through the interface: objects are static.
    ~IBatteryCharger() = default;
};

} // namespace hal
