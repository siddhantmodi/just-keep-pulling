/**
 * @file led.cpp
 * @brief led_svc::Led implementation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "led.hpp"

#include <zephyr/logging/log.h>

#include <initializer_list>
#include <utility>

LOG_MODULE_REGISTER( led_svc, CONFIG_APP_LED_SVC_LOG_LEVEL );

namespace led_svc
{

fw::Result<> Led::init()
{
    for( hal::IDigitalOutput *output : { &red_, &green_, &blue_ } ) {
        if( auto ret = output->init( false ); !ret ) {
            LOG_ERR( "LED not ready: %d", fw::err::to_zephyr( ret.error() ) );
            return ret;
        }
    }
    color_ = Color::off;
    return {};
}

fw::Result<> Led::show( Color color )
{
    // All three are set, so a failure leaves at worst a different single color, never a mix.
    fw::Result<> result{};
    for( auto [output, own] : { std::pair{ &red_, Color::red }, std::pair{ &green_, Color::green }, std::pair{ &blue_, Color::blue } } ) {
        if( auto ret = output->set( color == own ); !ret && result ) {
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
