/**
 * @file pmic.hpp
 * @brief pmic_svc::Pmic: the PMIC over an injected hal::IBatteryCharger and hal::IPowerOff.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "pmic_svc.hpp"

#include <fw/error.hpp>
#include <hal/battery_charger.hpp>
#include <hal/power_off.hpp>

namespace pmic_svc
{

class Pmic final : public IPmic
{
public:
    constexpr Pmic( hal::IBatteryCharger &charger, hal::IPowerOff &power_off ) noexcept
        : charger_{ charger }
        , power_off_{ power_off }
    {
    }

    [[nodiscard]] fw::Result<> init() override;
    [[nodiscard]] fw::Result<hal::BatteryStatus> read_battery() override;
    [[nodiscard]] fw::Result<> ship_mode() override;

private:
    hal::IBatteryCharger &charger_;
    hal::IPowerOff &power_off_;
};

} // namespace pmic_svc
