/**
 * @file main.cpp
 * @brief app_main entry point: a Tindeq Progressor-compatible finger-strength dynamometer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ble_svc.hpp"
#include "identity_svc.hpp"
#include "load_cell_svc.hpp"

#include <zephyr/app_version.h>
#include <zephyr/logging/log.h>

#include <cmath>
#include <fw/settings.hpp>

LOG_MODULE_REGISTER( app_main, CONFIG_APP_MAIN_LOG_LEVEL );

int main()
{
    LOG_INF( "app_main v%s", APP_VERSION_STRING );

    if( auto ret = fw::settings::init(); !ret ) {
        LOG_ERR( "settings init failed: %d", fw::err::to_zephyr( ret.error() ) );
    }

    if( auto ret = identity_svc::identity().init(); !ret ) {
        LOG_ERR( "identity init failed: %d", fw::err::to_zephyr( ret.error() ) );
    }

    if( auto ret = load_cell_svc::load_cell().init(); !ret ) {
        LOG_ERR( "load cell init failed: %d", fw::err::to_zephyr( ret.error() ) );
        return 0;
    }
    LOG_INF( "calibration: %ld ug per code", std::lround( load_cell_svc::load_cell().calibration_factor() * 1e6F ) );

    if( auto ret = ble_svc::init(); !ret ) {
        LOG_ERR( "BLE service init failed: %d", fw::err::to_zephyr( ret.error() ) );
        return 0;
    }
    return 0;
}
