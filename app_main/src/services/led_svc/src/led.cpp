/**
 * @file led.cpp
 * @brief led_svc::Led implementation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "led.hpp"

#include <zephyr/logging/log.h>

#include <expected>
#include <system_error>

LOG_MODULE_REGISTER( led_svc, CONFIG_APP_LED_SVC_LOG_LEVEL );

namespace led_svc
{

fw::Result<> Led::init()
{
    if( auto ret = output_.init( false ); !ret ) {
        LOG_ERR( "init failed: %d", fw::err::to_zephyr( ret.error() ) );
        return ret;
    }
    on_ = false;
    initialized_ = true;
    return {};
}

fw::Result<> Led::set( bool on )
{
    if( !initialized_ ) {
        return std::unexpected( std::errc::operation_not_permitted );
    }
    if( auto ret = output_.set( on ); !ret ) {
        LOG_ERR( "set(%d) failed: %d", on, fw::err::to_zephyr( ret.error() ) );
        return ret;
    }
    on_ = on;
    LOG_INF( "LED %s", on ? "on" : "off" );
    return {};
}

bool Led::is_on() const
{
    return on_;
}

} // namespace led_svc
