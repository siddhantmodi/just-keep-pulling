/**
 * @file main.cpp
 * @brief app_main entry point.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "app_manager.hpp"
#include "ble_svc.hpp"
#include "led_svc.hpp"
#include "temp_svc.hpp"

#include <zephyr/app_version.h>
#include <zephyr/logging/log.h>

#include <cstdlib>
#include <fw/error.hpp>

LOG_MODULE_REGISTER( app_main, CONFIG_APP_MAIN_LOG_LEVEL );

int main()
{
    LOG_INF( "app_main v%s", APP_VERSION_STRING );

    if( auto ret = led_svc::led().init(); !ret ) {
        LOG_ERR( "LED service init failed: %d", fw::err::to_zephyr( ret.error() ) );
        return 0;
    }

    if( auto ret = temp_svc::temperature().init(); !ret ) {
        LOG_ERR( "temperature service init failed: %d", fw::err::to_zephyr( ret.error() ) );
        return 0;
    }

    if( auto centicelsius = temp_svc::temperature().read_centicelsius(); centicelsius ) {
        int const magnitude = std::abs( *centicelsius );
        LOG_INF( "die temperature: %s%d.%02d C", *centicelsius < 0 ? "-" : "", magnitude / 100, magnitude % 100 );
    }

    if( auto ret = app_manager::start(); !ret ) {
        LOG_ERR( "app manager start failed: %d", fw::err::to_zephyr( ret.error() ) );
        return 0;
    }

    if( auto ret = ble_svc::init(); !ret ) {
        LOG_ERR( "BLE service init failed: %d", fw::err::to_zephyr( ret.error() ) );
        return 0;
    }

    return 0;
}
