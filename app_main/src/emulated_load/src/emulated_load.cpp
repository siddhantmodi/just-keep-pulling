/**
 * @file emulated_load.cpp
 * @brief Emulated pulls for the ADS1220 emulator: the force ramps up to a peak while the user button is held,
 *        or the shell holds a fixed ADC code ("emul raw <code>", "emul button").
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "calibration.hpp"

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <jkp/drivers/ads1220_emul.h>
#include <system_error>

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
constinit std::atomic<bool> holding{ false };
constinit std::atomic<std::int32_t> held_raw{ 0 };

/** @brief The emulator's source: runs in its timer callback (interrupt context), once per conversion. */
std::int32_t next_conversion( void * /*context*/, std::uint32_t /*index*/ )
{
    if( holding.load( std::memory_order_relaxed ) ) {
        return held_raw.load( std::memory_order_relaxed );
    }
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

#if defined( CONFIG_SHELL )
int cmd_raw( const shell *sh, size_t /*argc*/, char **argv )
{
    std::int32_t raw = 0;
    const char *const end = argv[1] + std::strlen( argv[1] );
    if( auto [parsed_end, error] = std::from_chars( argv[1], end, raw ); error != std::errc{} || parsed_end != end ) {
        shell_error( sh, "expected an ADC code" );
        return -EINVAL;
    }
    held_raw.store( raw, std::memory_order_relaxed );
    holding.store( true, std::memory_order_relaxed );
    shell_print( sh, "holding %d", static_cast<int>( raw ) );
    return 0;
}

int cmd_button( const shell *sh, size_t /*argc*/, char ** /*argv*/ )
{
    holding.store( false, std::memory_order_relaxed );
    shell_print( sh, "pulls on the user button" );
    return 0;
}
#endif

} // namespace

SYS_INIT( init, APPLICATION, 99 );

#if defined( CONFIG_SHELL )
// NOLINTBEGIN(modernize-avoid-c-arrays,modernize-use-nullptr,performance-no-int-to-ptr)
SHELL_STATIC_SUBCMD_SET_CREATE( emul_cmds, SHELL_CMD_ARG( raw, NULL, "Hold a fixed ADC code: emul raw <code>", cmd_raw, 2, 0 ),
                                SHELL_CMD( button, NULL, "Back to pulls on the user button", cmd_button ), SHELL_SUBCMD_SET_END );
SHELL_CMD_REGISTER( emul, &emul_cmds, "Emulated load cell", NULL );
// NOLINTEND(modernize-avoid-c-arrays,modernize-use-nullptr,performance-no-int-to-ptr)
#endif
