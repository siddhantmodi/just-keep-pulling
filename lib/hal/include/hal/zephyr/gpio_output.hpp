/**
 * @file gpio_output.hpp
 * @brief hal::zephyr::GpioOutput: hal::IDigitalOutput over the Zephyr GPIO API.
 *
 * Constructed from a devicetree spec, so the app (not the HAL) decides which
 * pin is used. The spec carries no direction: whichever pin is passed in is
 * configured as an output.
 * @code
 * constinit hal::zephyr::GpioOutput led{ GPIO_DT_SPEC_GET( DT_ALIAS( led0 ), gpios ) };
 * @endcode
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/drivers/gpio.h>

#include <expected>
#include <fw/error.hpp>
#include <hal/digital_output.hpp>
#include <system_error>

namespace hal::zephyr
{

class GpioOutput final : public IDigitalOutput
{
public:
    constexpr explicit GpioOutput( gpio_dt_spec spec ) noexcept
        : spec_{ spec }
    {
    }

    [[nodiscard]] fw::Result<> init( bool active ) override
    {
        if( !gpio_is_ready_dt( &spec_ ) ) {
            return std::unexpected( std::errc::no_such_device );
        }
        return fw::err::check( gpio_pin_configure_dt( &spec_, active ? GPIO_OUTPUT_ACTIVE : GPIO_OUTPUT_INACTIVE ) );
    }

    [[nodiscard]] fw::Result<> set( bool active ) override
    {
        return fw::err::check( gpio_pin_set_dt( &spec_, active ? 1 : 0 ) );
    }

private:
    gpio_dt_spec spec_;
};

} // namespace hal::zephyr
