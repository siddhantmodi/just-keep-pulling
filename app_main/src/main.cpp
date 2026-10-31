/**
 * @file main.cpp
 * @brief app_main entry point: a Tindeq Progressor-compatible finger-strength dynamometer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ble_svc.hpp"
#include "identity_svc.hpp"
#include "led_svc.hpp"
#include "load_cell_svc.hpp"
#include "measurement_manager.hpp"
#include "motion_svc.hpp"
#include "pmic_svc.hpp"
#include "power_manager.hpp"

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

    // Without the LED the device still works; it just doesn't show its state.
    if( auto ret = led_svc::led().init(); !ret ) {
        LOG_ERR( "LED init failed: %d", fw::err::to_zephyr( ret.error() ) );
    }

    // Without the IMU the device still measures; it then only wakes from idle sleep on a power cycle.
    if( auto ret = motion_svc::motion().init(); !ret ) {
        LOG_ERR( "motion init failed: %d", fw::err::to_zephyr( ret.error() ) );
    }

    // Without the PMIC the device still measures; only battery reports and low-battery power-off are lost.
    if( auto ret = pmic_svc::pmic().init(); !ret ) {
        LOG_ERR( "PMIC init failed: %d", fw::err::to_zephyr( ret.error() ) );
    } else if( auto started = power_manager::start(); !started ) {
        LOG_ERR( "first battery reading failed: %d", fw::err::to_zephyr( started.error() ) );
    }

    // Before Bluetooth, so the manager observes the first command.
    if( auto ret = measurement_manager::start(); !ret ) {
        LOG_ERR( "measurement manager start failed: %d", fw::err::to_zephyr( ret.error() ) );
        return 0;
    }

    if( auto ret = ble_svc::init(); !ret ) {
        LOG_ERR( "BLE service init failed: %d", fw::err::to_zephyr( ret.error() ) );
        return 0;
    }
    return 0;
}
