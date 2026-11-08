/**
 * @file led.cpp
 * @brief led_svc::Led implementation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "led.hpp"

#include <zephyr/logging/log.h>

#include <expected>
#include <initializer_list>
#include <system_error>
#include <utility>

LOG_MODULE_REGISTER( led_svc, CONFIG_APP_LED_SVC_LOG_LEVEL );

namespace led_svc
{

fw::Result<> Led::init()
{
    for( const led_dt_spec *led : { &red_, &green_, &blue_ } ) {
        if( !led_is_ready_dt( led ) ) {
            LOG_ERR( "LED not ready" );
            return std::unexpected( std::errc::no_such_device );
        }
    }
    return show( Color::off );
}

fw::Result<> Led::show( Color color )
{
    // All three are set, so a failure leaves at worst a different single color, never a mix.
    fw::Result<> result{};
    for( auto [led, own] : { std::pair{ &red_, Color::red }, std::pair{ &green_, Color::green }, std::pair{ &blue_, Color::blue } } ) {
        if( auto ret = fw::err::check( color == own ? led_on_dt( led ) : led_off_dt( led ) ); !ret && result ) {
            result = ret;
        }
    }
    color_ = color;
    return result;
}

Color Led::color() const
{
    return color_;
}

} // namespace led_svc
