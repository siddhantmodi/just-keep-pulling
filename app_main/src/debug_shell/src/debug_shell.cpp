/**
 * @file debug_shell.cpp
 * @brief Debug-build shell commands: jkp id [set <hex>], jkp rate [set <SPS>], jkp battery,
 *        jkp tare, jkp measure <s>, jkp calibrate <kg>|reset.
 *
 * Built only when debug.conf enables the shell. Commands run in the shell
 * thread, so they only use the services' thread-safe calls: changing the ID
 * (picked up on the next boot) and the configured sample rate (picked up on
 * the next measurement). The battery comes from the power manager's latest
 * reading on zbus. Tare, measure and calibrate publish the commands a client
 * would, for the measurement manager; their results are in its log.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "channels.hpp"
#include "identity_svc.hpp"
#include "load_cell_svc.hpp"
#include "progressor.hpp"

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fw/error.hpp>
#include <hal/battery_charger.hpp>
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
    shell_print( sh, "device ID saved; used from the next boot" );
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

const char *charge_name( hal::ChargeState charge )
{
    switch( charge ) {
    case hal::ChargeState::trickle:
        return "trickle charging";
    case hal::ChargeState::constant_current:
        return "charging (constant current)";
    case hal::ChargeState::constant_voltage:
        return "charging (constant voltage)";
    case hal::ChargeState::complete:
        return "charge complete";
    case hal::ChargeState::idle:
        break;
    }
    return "not charging";
}

int cmd_battery( const shell *sh, size_t /*argc*/, char ** /*argv*/ )
{
    auto battery = channels::battery.read( channels::lock_timeout );
    if( !battery ) {
        shell_error( sh, "no reading: %d", fw::err::to_zephyr( battery.error() ) );
        return fw::err::to_zephyr( battery.error() );
    }
    hal::BatteryStatus const &status = battery->status;
    shell_print( sh, "%u %%, %u mV, %d uA, die %d.%01d C, %s, USB %s", battery->percent, static_cast<unsigned>( status.millivolts ),
                 static_cast<int>( status.microamps ), static_cast<int>( status.die_millicelsius / 1000 ),
                 static_cast<int>( std::abs( status.die_millicelsius / 100 ) % 10 ), charge_name( status.charge ),
                 status.external_power ? "present" : "absent" );
    return 0;
}

int publish( const shell *sh, progressor::Opcode opcode )
{
    if( auto ret = channels::progressor_command.publish( { .opcode = opcode }, channels::lock_timeout ); !ret ) {
        shell_error( sh, "not sent: %d", fw::err::to_zephyr( ret.error() ) );
        return fw::err::to_zephyr( ret.error() );
    }
    return 0;
}

int publish( const shell *sh, const channels::ConfigCommand &command )
{
    if( auto ret = channels::config_command.publish( command, channels::lock_timeout ); !ret ) {
        shell_error( sh, "not sent: %d", fw::err::to_zephyr( ret.error() ) );
        return fw::err::to_zephyr( ret.error() );
    }
    return 0;
}

int cmd_tare( const shell *sh, size_t /*argc*/, char ** /*argv*/ )
{
    return publish( sh, progressor::Opcode::tare );
}

constexpr unsigned max_measure_seconds = 3600;

int cmd_measure( const shell *sh, size_t /*argc*/, char **argv )
{
    unsigned seconds = 0;
    const char *const end = argv[1] + std::strlen( argv[1] );
    auto [parsed_end, parse_error] = std::from_chars( argv[1], end, seconds );
    if( parse_error != std::errc{} || parsed_end != end || seconds == 0 || seconds > max_measure_seconds ) {
        shell_error( sh, "expected 1 to %u seconds", max_measure_seconds );
        return -EINVAL;
    }
    if( auto ret = publish( sh, progressor::Opcode::start_measurement ); ret != 0 ) {
        return ret;
    }
    k_sleep( K_SECONDS( seconds ) );
    return publish( sh, progressor::Opcode::stop_measurement );
}

int cmd_calibrate( const shell *sh, size_t /*argc*/, char **argv )
{
    if( std::strcmp( argv[1], "reset" ) == 0 ) {
        return publish( sh, { .type = channels::ConfigCommand::Type::reset_calibration, .calibration_kg = 0.0F, .samples_per_second = 0 } );
    }
    char *end = nullptr;
    float const kg = std::strtof( argv[1], &end );
    if( end == argv[1] || *end != '\0' || !std::isfinite( kg ) || kg < 0.0F ) {
        shell_error( sh, "expected a load in kg (0 or more), or reset" );
        return -EINVAL;
    }
    return publish( sh, { .type = channels::ConfigCommand::Type::add_calibration_point, .calibration_kg = kg, .samples_per_second = 0 } );
}

} // namespace

// The Zephyr shell macros declare C arrays and compare the subcommand and handler with NULL,
// so passing nullptr does not compile: use NULL here (which the macros then cast to pointers).
// NOLINTBEGIN(modernize-avoid-c-arrays,modernize-use-nullptr,performance-no-int-to-ptr)
SHELL_STATIC_SUBCMD_SET_CREATE( jkp_id_cmds, SHELL_CMD_ARG( set, NULL, "Set the device ID: jkp id set <12 hex digits>", cmd_id_set, 2, 0 ),
                                SHELL_SUBCMD_SET_END );
SHELL_STATIC_SUBCMD_SET_CREATE( jkp_rate_cmds, SHELL_CMD_ARG( set, NULL, "Set the sample rate: jkp rate set <SPS>", cmd_rate_set, 2, 0 ),
                                SHELL_SUBCMD_SET_END );
SHELL_STATIC_SUBCMD_SET_CREATE( jkp_cmds, SHELL_CMD( id, &jkp_id_cmds, "Show the device ID; jkp id set <12 hex digits> sets it", cmd_id ),
                                SHELL_CMD( rate, &jkp_rate_cmds, "Show the sample rate; jkp rate set <SPS> sets it", cmd_rate ),
                                SHELL_CMD( battery, NULL, "Show the latest battery reading and state of charge (every 5 s)", cmd_battery ),
                                SHELL_CMD( tare, NULL, "Zero the scale, as the app's tare", cmd_tare ),
                                SHELL_CMD_ARG( measure, NULL, "Measure for <seconds> without a client; the statistics are logged at the end",
                                               cmd_measure, 2, 0 ),
                                SHELL_CMD_ARG( calibrate, NULL, "Add a calibration point under <kg>, or reset to the default factor", cmd_calibrate,
                                               2, 0 ),
                                SHELL_SUBCMD_SET_END );
SHELL_CMD_REGISTER( jkp, &jkp_cmds, "Dynamometer debug commands", NULL );
// NOLINTEND(modernize-avoid-c-arrays,modernize-use-nullptr,performance-no-int-to-ptr)
