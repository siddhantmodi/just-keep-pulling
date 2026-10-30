/**
 * @file main.cpp
 * @brief app_main entry point.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "led_svc.hpp"

#include <zephyr/app_version.h>
#include <zephyr/logging/log.h>

#include <fw/error.hpp>

LOG_MODULE_REGISTER( app_main, CONFIG_APP_MAIN_LOG_LEVEL );

int main()
{
    LOG_INF( "app_main v%s", APP_VERSION_STRING );

    if( auto ret = led_svc::init(); !ret ) {
        LOG_ERR( "LED service init failed: %d", fw::err::to_zephyr( ret.error() ) );
        return 0;
    }

    return 0;
}
