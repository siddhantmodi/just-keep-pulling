/**
 * @file motion.cpp
 * @brief motion_svc::Motion implementation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "motion.hpp"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER( motion_svc, CONFIG_APP_MOTION_SVC_LOG_LEVEL );

namespace motion_svc
{

fw::Result<> Motion::init()
{
    if( auto ret = sensor_.init(); !ret ) {
        LOG_ERR( "IMU not ready: %d", fw::err::to_zephyr( ret.error() ) );
        return ret;
    }
    if( auto ret = system_off_.init(); !ret ) {
        LOG_ERR( "wake pin not ready: %d", fw::err::to_zephyr( ret.error() ) );
        return ret;
    }
    return {};
}

bool Motion::moved()
{
    return sensor_.moved();
}

fw::Result<> Motion::sleep_until_moved()
{
    LOG_INF( "sleeping until moved" );
    auto ret = system_off_.power_off();
    if( !ret ) {
        LOG_WRN( "not sleeping: %d", fw::err::to_zephyr( ret.error() ) );
    }
    return ret;
}

} // namespace motion_svc
