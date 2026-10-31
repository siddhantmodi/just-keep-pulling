/**
 * @file debug_shell.cpp
 * @brief Debug-build shell commands: jkp id [set <hex>], jkp rate [set <SPS>], jkp battery.
 *
 * Built only when debug.conf enables the shell. Commands run in the shell
 * thread, so they only use the services' thread-safe calls: changing the ID
 * (picked up on the next boot) and the configured sample rate (picked up on
 * the next measurement). The battery comes from the power manager's latest
 * reading on zbus.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "channels.hpp"
#include "identity_svc.hpp"
#include "load_cell_svc.hpp"

#include <zephyr/shell/shell.h>

#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fw/error.hpp>
#include <hal/load_cell_adc.hpp>
#include <system_error>

namespace
{

int cmd_id( const shell *sh, size_t /*argc*/, char ** /*argv*/ )
{
    shell_print( sh, "%s", identity_svc::format_device_id( identity_svc::identity().id() ).data() );
    return 0;
}

int cmd_id_set( const shell *sh, size_t /*argc*/, char **argv )
{
    auto id = identity_svc::parse_device_id( argv[1] );
    if( !id ) {
        shell_error( sh, "expected %u hex digits", static_cast<unsigned>( identity_svc::device_id_hex_length ) );
        return -EINVAL;
    }
    if( auto ret = identity_svc::identity().set_id( *id ); !ret ) {
        shell_error( sh, "not set: %d", fw::err::to_zephyr( ret.error() ) );
        return fw::err::to_zephyr( ret.error() );
    }
    shell_print( sh, "device ID set; the advertised name changes on the next boot" );
    return 0;
}

int cmd_rate( const shell *sh, size_t /*argc*/, char ** /*argv*/ )
{
    shell_print( sh, "%u SPS", hal::samples_per_second( load_cell_svc::load_cell().sample_rate() ) );
    return 0;
}

void print_supported_rates( const shell *sh )
{
    shell_fprintf( sh, SHELL_ERROR, "supported rates:" );
    for( hal::SampleRate const rate : hal::all_sample_rates ) {
        shell_fprintf( sh, SHELL_ERROR, " %u", hal::samples_per_second( rate ) );
    }
    shell_fprintf( sh, SHELL_ERROR, "\n" );
}

int cmd_rate_set( const shell *sh, size_t /*argc*/, char **argv )
{
    std::uint16_t samples_per_second = 0;
    const char *const end = argv[1] + std::strlen( argv[1] );
    auto [parsed_end, parse_error] = std::from_chars( argv[1], end, samples_per_second );
    auto rate = ( parse_error == std::errc{} && parsed_end == end ) ? hal::to_sample_rate( samples_per_second )
                                                                    : std::unexpected( std::errc::invalid_argument );
    if( !rate ) {
        print_supported_rates( sh );
        return -EINVAL;
    }
    if( auto ret = load_cell_svc::load_cell().set_sample_rate( *rate ); !ret ) {
        shell_error( sh, "not set: %d", fw::err::to_zephyr( ret.error() ) );
        return fw::err::to_zephyr( ret.error() );
    }
    shell_print( sh, "sample rate set; it applies from the next measurement" );
    return 0;
}

int cmd_battery( const shell *sh, size_t /*argc*/, char ** /*argv*/ )
{
    auto battery = channels::battery.read( K_MSEC( 100 ) );
    if( !battery ) {
        shell_error( sh, "no reading: %d", fw::err::to_zephyr( battery.error() ) );
        return fw::err::to_zephyr( battery.error() );
    }
    shell_print( sh, "%u mV, %s, USB %s", static_cast<unsigned>( battery->millivolts ), battery->charging ? "charging" : "not charging",
                 battery->external_power ? "present" : "absent" );
    return 0;
}

} // namespace

// The Zephyr shell macros declare C arrays and compare the subcommand and handler with NULL,
// so passing nullptr does not compile: use NULL here (which the macros then cast to pointers).
// NOLINTBEGIN(modernize-avoid-c-arrays,modernize-use-nullptr,performance-no-int-to-ptr)
SHELL_STATIC_SUBCMD_SET_CREATE( jkp_id_cmds, SHELL_CMD_ARG( set, NULL, "Set the device ID: jkp id set <12 hex digits>", cmd_id_set, 2, 0 ),
                                SHELL_SUBCMD_SET_END );
SHELL_STATIC_SUBCMD_SET_CREATE( jkp_rate_cmds, SHELL_CMD_ARG( set, NULL, "Set the sample rate: jkp rate set <SPS>", cmd_rate_set, 2, 0 ),
                                SHELL_SUBCMD_SET_END );
SHELL_STATIC_SUBCMD_SET_CREATE( jkp_cmds, SHELL_CMD( id, &jkp_id_cmds, "Show the device ID", cmd_id ),
                                SHELL_CMD( rate, &jkp_rate_cmds, "Show the sample rate", cmd_rate ),
                                SHELL_CMD( battery, NULL, "Show the latest battery reading (every 30 s)", cmd_battery ), SHELL_SUBCMD_SET_END );
SHELL_CMD_REGISTER( jkp, &jkp_cmds, "Dynamometer debug commands", NULL );
// NOLINTEND(modernize-avoid-c-arrays,modernize-use-nullptr,performance-no-int-to-ptr)
