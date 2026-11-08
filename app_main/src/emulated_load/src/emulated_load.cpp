/**
 * @file emulated_load.cpp
 * @brief Emulated pulls for the ADS1220 emulator: the force ramps up to a peak while the user button is held.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "calibration.hpp"

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <jkp/drivers/ads1220_emul.h>

namespace
{

constexpr auto codes_per_kg = static_cast<std::int32_t>( 1000.0F / calibration::default_factor );
constexpr std::int32_t peak = CONFIG_APP_EMULATED_LOAD_PEAK_KG * codes_per_kg;
constexpr std::int64_t ramp_ms = 300;
constexpr std::uint32_t noise_codes = 200; // about 20 g

constexpr gpio_dt_spec button = GPIO_DT_SPEC_GET( DT_ALIAS( sw0 ), gpios );

constinit std::int32_t level = 0;
constinit std::int64_t last_ms = 0;
constinit std::uint32_t noise_state = 1;

/** @brief The emulator's source: runs in its timer callback (interrupt context), once per conversion. */
std::int32_t next_conversion( void * /*context*/, std::uint32_t /*index*/ )
{
    std::int64_t const now = k_uptime_get();
    auto const step = static_cast<std::int32_t>( peak * std::min( now - last_ms, ramp_ms ) / ramp_ms );
    last_ms = now;
    std::int32_t const target = gpio_pin_get_dt( &button ) > 0 ? peak : 0;
    level = target > level ? std::min( level + step, target ) : std::max( level - step, target );
    noise_state = ( noise_state * 1103515245U ) + 12345U;
    return level + static_cast<std::int32_t>( ( noise_state >> 16U ) % ( ( 2U * noise_codes ) + 1U ) ) - static_cast<std::int32_t>( noise_codes );
}

int init()
{
    if( !gpio_is_ready_dt( &button ) || gpio_pin_configure_dt( &button, GPIO_INPUT ) != 0 ) {
        return -ENODEV;
    }
    ads1220_emul_set_source( EMUL_DT_GET( DT_NODELABEL( load_cell_adc ) ), next_conversion, nullptr );
    return 0;
}

} // namespace

SYS_INIT( init, APPLICATION, 99 );
