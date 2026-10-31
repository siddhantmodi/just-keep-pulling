/**
 * @file pmic_svc.hpp
 * @brief PMIC service public API: the IPmic interface and the app's nPM1300.
 *
 * Not thread-safe: call from a single thread (the power manager). Others get
 * the latest battery reading from channels::battery.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fw/error.hpp>
#include <hal/battery_charger.hpp>

namespace pmic_svc
{

class IPmic
{
public:
    /** @brief Check the charger and the power switch are usable. Fails with no_such_device otherwise. */
    [[nodiscard]] virtual fw::Result<> init() = 0;

    /** @brief Measure the battery and read the charger's state. */
    [[nodiscard]] virtual fw::Result<hal::BatteryStatus> read_battery() = 0;

    /**
     * @brief Enter ship mode: every rail off, a fraction of a microamp from the
     *        battery. The device comes back on when USB power is connected or
     *        the battery is reconnected (the power switch).
     */
    [[nodiscard]] virtual fw::Result<> ship_mode() = 0;

protected:
    // Never deleted through the interface: objects are static.
    ~IPmic() = default;
};

/** @brief The app's PMIC (devicetree aliases pmic-charger and pmic-regulators). */
[[nodiscard]] IPmic &pmic();

} // namespace pmic_svc
