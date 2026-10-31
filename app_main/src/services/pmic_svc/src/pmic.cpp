/**
 * @file pmic.cpp
 * @brief pmic_svc::Pmic implementation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pmic.hpp"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER( pmic_svc, CONFIG_APP_PMIC_SVC_LOG_LEVEL );

namespace pmic_svc
{

fw::Result<> Pmic::init()
{
    if( auto ret = charger_.init(); !ret ) {
        LOG_ERR( "charger not ready: %d", fw::err::to_zephyr( ret.error() ) );
        return ret;
    }
    if( auto ret = power_off_.init(); !ret ) {
        LOG_ERR( "regulators not ready: %d", fw::err::to_zephyr( ret.error() ) );
        return ret;
    }
    return {};
}

fw::Result<hal::BatteryStatus> Pmic::read_battery()
{
    auto status = charger_.read();
    if( !status ) {
        LOG_ERR( "battery read failed: %d", fw::err::to_zephyr( status.error() ) );
        return status;
    }
    LOG_DBG( "battery %u mV%s%s", static_cast<unsigned>( status->millivolts ), status->charging ? ", charging" : "",
             status->external_power ? ", USB" : "" );
    return status;
}

fw::Result<> Pmic::ship_mode()
{
    LOG_INF( "entering ship mode" );
    auto ret = power_off_.power_off();
    if( !ret ) {
        LOG_ERR( "ship mode failed: %d", fw::err::to_zephyr( ret.error() ) );
    }
    return ret;
}

} // namespace pmic_svc
