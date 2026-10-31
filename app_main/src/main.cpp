/**
 * @file main.cpp
 * @brief app_main entry point: a Tindeq Progressor-compatible finger-strength dynamometer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/app_version.h>
#include <zephyr/logging/log.h>

#include <fw/settings.hpp>

LOG_MODULE_REGISTER( app_main, CONFIG_APP_MAIN_LOG_LEVEL );

int main()
{
    LOG_INF( "app_main v%s", APP_VERSION_STRING );

    if( auto ret = fw::settings::init(); !ret ) {
        LOG_ERR( "settings init failed: %d", fw::err::to_zephyr( ret.error() ) );
    }
    return 0;
}
