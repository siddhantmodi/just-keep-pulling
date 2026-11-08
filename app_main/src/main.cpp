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
#if defined( CONFIG_BOOTLOADER_MCUBOOT )
#include <zephyr/dfu/mcuboot.h>
#endif
#if defined( CONFIG_HWINFO )
#include <zephyr/drivers/hwinfo.h>
#endif

#include <array>
#include <cmath>
#include <cstdint>
#include <fw/settings.hpp>

LOG_MODULE_REGISTER( app_main, CONFIG_APP_MAIN_LOG_LEVEL );

#if defined( CONFIG_HWINFO )
namespace
{

/** @brief Debug builds: why the chip reset, cleared so the next boot shows only its own. */
void log_reset_cause()
{
    std::uint32_t cause = 0;
    if( hwinfo_get_reset_cause( &cause ) != 0 ) {
        return;
    }
    struct Name {
        std::uint32_t bit;
        const char *name;
    };
    constexpr std::array names{ Name{ .bit = RESET_POR, .name = "power-on" },
                                Name{ .bit = RESET_PIN, .name = "reset pin" },
                                Name{ .bit = RESET_SOFTWARE, .name = "software" },
                                Name{ .bit = RESET_BROWNOUT, .name = "brownout" },
                                Name{ .bit = RESET_WATCHDOG, .name = "watchdog" },
                                Name{ .bit = RESET_DEBUG, .name = "debugger" },
                                Name{ .bit = RESET_LOW_POWER_WAKE, .name = "wake from System OFF" },
                                Name{ .bit = RESET_CPU_LOCKUP, .name = "CPU lockup" } };
    LOG_INF( "reset cause 0x%08x", cause );
    for( auto const &[bit, name] : names ) {
        if( ( cause & bit ) != 0U ) {
            LOG_INF( "reset cause: %s", name );
        }
    }
    (void)hwinfo_clear_reset_cause();
}

} // namespace
#endif

int main()
{
    LOG_INF( "app_main v%s", APP_VERSION_STRING );
#if defined( CONFIG_HWINFO )
    log_reset_cause();
#endif

    if( auto ret = fw::settings::init(); !ret ) {
        LOG_ERR( "settings init failed: %d", fw::err::to_zephyr( ret.error() ) );
    }

    if( auto ret = identity_svc::identity().init(); !ret ) {
        LOG_ERR( "identity init failed: %d", fw::err::to_zephyr( ret.error() ) );
    }

    auto const load_cell_ready = load_cell_svc::load_cell().init();
    if( !load_cell_ready ) {
        LOG_ERR( "load cell init failed: %d", fw::err::to_zephyr( load_cell_ready.error() ) );
    } else {
        LOG_INF( "calibration: %ld ug per code", std::lround( load_cell_svc::load_cell().calibration_factor() * 1e6F ) );
    }

    // Without the LED the device still works; it just doesn't show its state.
    if( auto ret = led_svc::led().init(); !ret ) {
        LOG_ERR( "LED init failed: %d", fw::err::to_zephyr( ret.error() ) );
    }

    // Without the IMU the device still measures; it then only wakes from idle sleep on a power cycle.
    if( auto ret = motion_svc::motion().init(); !ret ) {
        LOG_ERR( "motion init failed: %d", fw::err::to_zephyr( ret.error() ) );
    }

    // Without the PMIC the device still measures; only battery reports and low-battery power-off are lost.
    auto const pmic_ready = pmic_svc::pmic().init();
    if( !pmic_ready ) {
        LOG_ERR( "PMIC init failed: %d", fw::err::to_zephyr( pmic_ready.error() ) );
    }
    if( auto started = power_manager::start( pmic_ready.has_value() ); !started ) {
        LOG_ERR( "first battery reading failed: %d", fw::err::to_zephyr( started.error() ) );
    }

    // Without the load cell there is nothing to measure: the device still
    // advertises, so it can be found (device information, battery), but Progressor commands
    // and calibration requests go unanswered. Before Bluetooth, so the manager
    // observes the first command.
    if( load_cell_ready ) {
        if( auto ret = measurement_manager::start(); !ret ) {
            LOG_ERR( "measurement manager start failed: %d", fw::err::to_zephyr( ret.error() ) );
        }
    }

    if( auto ret = ble_svc::init(); !ret ) {
        LOG_ERR( "BLE service init failed: %d", fw::err::to_zephyr( ret.error() ) );
        return 0;
    }

#if defined( CONFIG_BOOTLOADER_MCUBOOT )
    // A new image runs on trial until here: if it fails before, MCUboot goes back to the previous one at the next reset.
    if( !boot_is_img_confirmed() ) {
        if( auto ret = fw::err::check( boot_write_img_confirmed() ); ret ) {
            LOG_INF( "new firmware confirmed" );
        } else {
            LOG_ERR( "new firmware not confirmed, the previous one returns at the next reset: %d", fw::err::to_zephyr( ret.error() ) );
        }
    }
#endif

    return 0;
}
