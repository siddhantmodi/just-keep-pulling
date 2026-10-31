/**
 * @file die_temperature.cpp
 * @brief temp_svc::DieTemperature implementation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "die_temperature.hpp"

#include <zephyr/logging/log.h>

#include <fw/scoped_lock.hpp>

LOG_MODULE_REGISTER( temp_svc, CONFIG_APP_TEMP_SVC_LOG_LEVEL );

namespace temp_svc
{

fw::Result<> DieTemperature::init()
{
    fw::ScopedLock const lock{ read_mutex_ };
    if( auto ret = sensor_.init(); !ret ) {
        LOG_ERR( "init failed: %d", fw::err::to_zephyr( ret.error() ) );
        return ret;
    }
    initialized_ = true;
    return {};
}

fw::Result<std::int16_t> DieTemperature::read_centicelsius()
{
    fw::ScopedLock const lock{ read_mutex_ };
    if( !initialized_ ) {
        return std::unexpected( std::errc::operation_not_permitted );
    }
    auto millicelsius = sensor_.read_millicelsius();
    if( !millicelsius ) {
        LOG_ERR( "read failed: %d", fw::err::to_zephyr( millicelsius.error() ) );
        return std::unexpected( millicelsius.error() );
    }
    return to_centicelsius( *millicelsius );
}

} // namespace temp_svc
