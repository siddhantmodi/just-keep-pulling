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

/** @brief What the charger is doing. */
enum class ChargeState : std::uint8_t {
    idle,
    trickle,
    constant_current,
    constant_voltage,
    complete,
};

struct BatteryStatus {
    std::uint32_t millivolts{};
    /** Battery current, as Zephyr's sensor API reports it: positive while charging, negative while discharging. */
    std::int32_t microamps{};
    /** The charger's die temperature: the closest to the battery's without a thermistor. */
    std::int32_t die_millicelsius{};
    ChargeState charge{ ChargeState::idle };
    /** External power (USB) is present, so the device runs whatever the battery's state. */
    bool external_power{};

    /** @brief The charger is charging the battery (trickle, constant current or constant voltage). */
    [[nodiscard]] constexpr bool charging() const noexcept
    {
        return charge == ChargeState::trickle || charge == ChargeState::constant_current || charge == ChargeState::constant_voltage;
    }
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
