/**
 * @file led_svc.cpp
 * @brief Production binding of led_svc::Led to the led0 GPIO behind the led_svc:: API.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "led_svc.hpp"

#include "led.hpp"

#include <zephyr/logging/log.h>

#include <hal/zephyr/gpio_output.hpp>

LOG_MODULE_REGISTER( led_svc, CONFIG_APP_LED_SVC_LOG_LEVEL );

namespace
{

constinit hal::zephyr::GpioOutput led_pin{ GPIO_DT_SPEC_GET( DT_ALIAS( led0 ), gpios ) };
constinit led_svc::Led<hal::zephyr::GpioOutput> led{ led_pin };

} // namespace

namespace led_svc
{

fw::Result<> init()
{
    auto ret = led.init();
    if( !ret ) {
        LOG_ERR( "init failed: %d", fw::err::to_zephyr( ret.error() ) );
    }
    return ret;
}

fw::Result<> set( bool on )
{
    auto ret = led.set( on );
    if( ret ) {
        LOG_DBG( "LED %s", on ? "on" : "off" );
    } else {
        LOG_ERR( "set(%d) failed: %d", on, fw::err::to_zephyr( ret.error() ) );
    }
    return ret;
}

fw::Result<> toggle()
{
    return set( !led.is_on() );
}

bool is_on()
{
    return led.is_on();
}

} // namespace led_svc
