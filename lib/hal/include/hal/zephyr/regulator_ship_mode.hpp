/**
 * @file regulator_ship_mode.hpp
 * @brief hal::zephyr::RegulatorShipMode: hal::IPowerOff as a PMIC's ship mode, through Zephyr's regulator API.
 *
 * Constructed from the PMIC's regulators parent device (e.g. the
 * nordic,npm1300-regulator node). Ship mode turns every rail off until the
 * PMIC is woken: on the nPM1300, by USB power, the SHPHLD pin, or the
 * battery being reconnected.
 * @code
 * constinit hal::zephyr::RegulatorShipMode ship_mode{ DEVICE_DT_GET( DT_NODELABEL( pmic_regulators ) ) };
 * @endcode
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/device.h>
#include <zephyr/drivers/regulator.h>

#include <expected>
#include <fw/error.hpp>
#include <hal/power_off.hpp>
#include <system_error>

namespace hal::zephyr
{

class RegulatorShipMode final : public IPowerOff
{
public:
    constexpr explicit RegulatorShipMode( const device *regulators ) noexcept
        : regulators_{ regulators }
    {
    }

    [[nodiscard]] fw::Result<> init() override
    {
        if( !device_is_ready( regulators_ ) ) {
            return std::unexpected( std::errc::no_such_device );
        }
        return {};
    }

    [[nodiscard]] fw::Result<> power_off() override
    {
        return fw::err::check( regulator_parent_ship_mode( regulators_ ) );
    }

private:
    const device *regulators_;
};

} // namespace hal::zephyr
