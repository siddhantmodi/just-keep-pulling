/**
 * @file gpio_wake_system_off.hpp
 * @brief hal::zephyr::GpioWakeSystemOff: hal::IPowerOff as the SoC's System OFF, woken by a GPIO.
 *
 * Arms the pin to wake the SoC when it becomes active (on Nordic SoCs, its
 * SENSE), then enters System OFF; waking restarts the firmware from reset.
 * Everything else stays powered, e.g. a sensor driving the pin. Boards without
 * Zephyr's poweroff support (native_sim) fail with not_supported.
 * @code
 * constinit hal::zephyr::GpioWakeSystemOff system_off{ GPIO_DT_SPEC_GET( DT_ALIAS( motion_sensor ), irq_gpios ) };
 * @endcode
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/poweroff.h>

#include <expected>
#include <fw/error.hpp>
#include <hal/power_off.hpp>
#include <system_error>

namespace hal::zephyr
{

class GpioWakeSystemOff final : public IPowerOff
{
public:
    constexpr explicit GpioWakeSystemOff( const gpio_dt_spec &wake ) noexcept
        : wake_{ wake }
    {
    }

    [[nodiscard]] fw::Result<> init() override
    {
        if( !gpio_is_ready_dt( &wake_ ) ) {
            return std::unexpected( std::errc::no_such_device );
        }
        return {};
    }

    [[nodiscard]] fw::Result<> power_off() override
    {
#if defined( CONFIG_POWEROFF )
        if( auto ret = fw::err::check( gpio_pin_configure_dt( &wake_, GPIO_INPUT ) ); !ret ) {
            return ret;
        }
        if( auto ret = fw::err::check( gpio_pin_interrupt_configure_dt( &wake_, GPIO_INT_LEVEL_ACTIVE ) ); !ret ) {
            return ret;
        }
        sys_poweroff();
#else
        return std::unexpected( std::errc::not_supported );
#endif
    }

private:
    gpio_dt_spec wake_;
};

} // namespace hal::zephyr
