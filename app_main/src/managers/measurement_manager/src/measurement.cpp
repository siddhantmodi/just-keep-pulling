/**
 * @file measurement.cpp
 * @brief measurement_manager::Measurement implementation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "measurement.hpp"

#include <zephyr/logging/log.h>

#include <algorithm>
#include <cmath>
#include <expected>
#include <span>
#include <system_error>

LOG_MODULE_REGISTER( measurement_manager, CONFIG_APP_MEASUREMENT_MANAGER_LOG_LEVEL );

namespace measurement_manager
{

namespace
{

std::uint32_t period_us( hal::SampleRate rate )
{
    return 1'000'000U / hal::samples_per_second( rate );
}

/** @brief A failure that only means nobody is listening: the client is gone or not subscribed. */
bool no_listener( std::errc error )
{
    return error == std::errc::not_connected;
}

} // namespace

fw::Result<> Measurement::start()
{
    // Readings are still usable untared, so a failure (logged by the load cell) is not fatal.
    if( auto ret = load_cell_.tare(); !ret ) {
        LOG_WRN( "starting untared" );
    }
    publish_calibration( channels::CalibrationStatus::ok );
    return {};
}

fw::Result<> Measurement::handle( const Event &event )
{
    switch( event.type ) {
    case Event::Type::command:
        return on_command( event.command );
    case Event::Type::client_connection:
        return on_client_connection( event.client_connection );
    case Event::Type::config_command:
        return on_config_command( event.config_command );
    }
    return std::unexpected( std::errc::invalid_argument );
}

fw::Result<> Measurement::on_command( const progressor::Command &command )
{
    using progressor::Opcode;

    switch( command.opcode ) {
    case Opcode::tare:
        // The pending samples were measured with the old tare.
        send_batch();
        return load_cell_.tare();
    case Opcode::start_measurement:
        return start_measurement();
    case Opcode::stop_measurement:
        return stop_measurement( true );
    case Opcode::get_app_version:
        return reply( progressor::encode_app_version( app_version ) );
    case Opcode::shutdown:
    {
        // As Crimpdeq: end the session and keep advertising. Power-off is the switch.
        LOG_INF( "shutdown requested, disconnecting" );
        auto stopped = stop_measurement( false );
        auto disconnected = client_.disconnect();
        return stopped ? disconnected : stopped;
    }
    case Opcode::sample_battery:
    {
        // The power manager's latest reading: measuring here would compete with it for the PMIC.
        auto battery = channels::battery.read( channels::lock_timeout );
        if( !battery ) {
            return std::unexpected( battery.error() );
        }
        return reply( progressor::encode_battery_voltage( battery->status.millivolts ) );
    }
    case Opcode::get_progressor_id:
        return reply( progressor::encode_progressor_id( id_ ) );
    }
    return std::unexpected( std::errc::not_supported );
}

fw::Result<> Measurement::on_client_connection( const channels::ClientConnection &connection )
{
    if( !connection.connected && measuring() ) {
        LOG_INF( "client disconnected, measurement stopped" );
        return stop_measurement( false );
    }
    return {};
}

fw::Result<> Measurement::on_config_command( const channels::ConfigCommand &command )
{
    using Type = channels::ConfigCommand::Type;

    switch( command.type ) {
    case Type::add_calibration_point:
        return add_calibration_point( command.calibration_kg );
    case Type::reset_calibration:
    {
        auto ret = load_cell_.reset_calibration();
        if( ret ) {
            LOG_INF( "calibration reset to the default factor" );
        }
        publish_calibration( ret ? channels::CalibrationStatus::ok : channels::CalibrationStatus::failed );
        return ret;
    }
    case Type::set_sample_rate:
    {
        auto rate = hal::to_sample_rate( command.samples_per_second );
        if( !rate ) {
            return std::unexpected( rate.error() );
        }
        return load_cell_.set_sample_rate( *rate );
    }
    }
    return std::unexpected( std::errc::invalid_argument );
}

fw::Result<> Measurement::add_calibration_point( float kg )
{
    // Tell the client it takes a while, and send the pending samples: the
    // measurement pauses while the point is averaged.
    publish_calibration( channels::CalibrationStatus::measuring );
    send_batch();

    auto points = load_cell_.add_calibration_point( kg );
    if( points ) {
        auto const factor_ug = std::lround( load_cell_.calibration_factor() * 1e6F );
        LOG_INF( "calibration point %u at %ld g; factor %ld.%03ld mg/code", static_cast<unsigned>( *points ), std::lround( kg * 1000.0F ),
                 factor_ug / 1000, factor_ug % 1000 );
        publish_calibration( channels::CalibrationStatus::ok );
        return {};
    }
    LOG_WRN( "calibration point not added: %d", fw::err::to_zephyr( points.error() ) );

    channels::CalibrationStatus status = channels::CalibrationStatus::failed;
    switch( points.error() ) {
    case std::errc::no_buffer_space:
        status = channels::CalibrationStatus::full;
        break;
    case std::errc::result_out_of_range: // an implausible factor
    case std::errc::invalid_argument:    // no line through the points
        status = channels::CalibrationStatus::fit_rejected;
        break;
    default:
        break;
    }
    publish_calibration( status );
    return std::unexpected( points.error() );
}

void Measurement::publish_calibration( channels::CalibrationStatus status ) const
{
    channels::CalibrationState const state{ .status = status,
                                            .points = static_cast<std::uint8_t>( load_cell_.calibration_points() ),
                                            .factor_g_per_code = load_cell_.calibration_factor() };
    if( auto ret = channels::calibration_state.publish( state, channels::lock_timeout ); !ret ) {
        LOG_ERR( "calibration state publish failed: %d", fw::err::to_zephyr( ret.error() ) );
    }
}

fw::Result<> Measurement::start_measurement()
{
    if( measuring() ) {
        // A new measurement on a new timeline, as Crimpdeq: the old one's pending samples are dropped.
        batch_size_ = 0;
        origin_us_.reset();
        stats_ = {};
        return {};
    }

    hal::SampleRate const rate = load_cell_.sample_rate();
    if( auto ret = load_cell_.start( rate ); !ret ) {
        LOG_ERR( "load cell start failed: %d", fw::err::to_zephyr( ret.error() ) );
        return ret;
    }
    rate_ = rate;
    origin_us_.reset();
    last_pull_us_.reset();
    batch_size_ = 0;
    read_failures_ = 0;
    stats_ = {};
    publish_state();
    LOG_INF( "measuring at %u SPS, %u samples per notification", hal::samples_per_second( rate ), static_cast<unsigned>( batch_capacity() ) );

    if( auto ret = client_.set_streaming( true ); !ret && !no_listener( ret.error() ) ) {
        LOG_WRN( "streaming connection parameters not requested: %d", fw::err::to_zephyr( ret.error() ) );
    }
    return {};
}

fw::Result<> Measurement::stop_measurement( bool send_pending )
{
    if( !measuring() ) {
        return {};
    }
    if( send_pending ) {
        send_batch();
    }
    batch_size_ = 0;
    rate_.reset();
    publish_state();

    auto stopped = load_cell_.stop();
    log_stats();
    if( auto ret = client_.set_streaming( false ); !ret && !no_listener( ret.error() ) ) {
        LOG_WRN( "idle connection parameters not requested: %d", fw::err::to_zephyr( ret.error() ) );
    }
    return stopped;
}

fw::Result<> Measurement::sample()
{
    if( !rate_ ) {
        return std::unexpected( std::errc::operation_not_permitted );
    }
    auto sample = load_cell_.read( hal::read_timeout( *rate_ ) );
    if( !sample ) {
        LOG_ERR( "load cell read failed: %d", fw::err::to_zephyr( sample.error() ) );
        if( ++read_failures_ >= max_read_failures ) {
            LOG_ERR( "load cell not responding, measurement stopped" );
            if( auto stopped = stop_measurement( true ); !stopped ) {
                LOG_ERR( "load cell stop failed: %d", fw::err::to_zephyr( stopped.error() ) );
            }
        }
        return std::unexpected( sample.error() );
    }
    read_failures_ = 0;
    stats_.add( *sample );

    if( !origin_us_ ) {
        origin_us_ = sample->timestamp_us - period_us( *rate_ );
    }
    if( batch_size_ == 0 ) {
        // Sized per batch: the client may exchange a larger MTU after the measurement started.
        batch_capacity_ = batch_capacity();
    }
    // The protocol's timestamps are 32-bit: they wrap after 71 minutes, as on a Progressor.
    batch_[batch_size_++] = { .kg = sample->kg, .timestamp_us = static_cast<std::uint32_t>( sample->timestamp_us - *origin_us_ ) };
    if( batch_size_ >= batch_capacity_ ) {
        send_batch();
    }
    report_pull( *sample );
    return {};
}

void Measurement::send_batch()
{
    if( batch_size_ == 0 ) {
        return;
    }
    // Cannot fail: the batch holds 1 to max_samples_per_packet samples.
    auto packet = progressor::encode_weight_measurements( std::span{ batch_ }.first( batch_size_ ) );
    batch_size_ = 0;
    auto sent = packet ? client_.send_progressor( packet->bytes() ) : std::unexpected( packet.error() );
    if( sent || no_listener( sent.error() ) ) {
        return;
    }
    if( sent.error() == std::errc::no_buffer_space ) {
        // Keep the newest samples flowing rather than stall sampling.
        if( dropped_batches_++ % 100 == 0 ) {
            LOG_WRN( "link too slow, %u batches dropped", static_cast<unsigned>( dropped_batches_ ) );
        }
        return;
    }
    LOG_ERR( "samples not sent: %d", fw::err::to_zephyr( sent.error() ) );
}

void Measurement::report_pull( const load_cell_svc::ForceSample &sample )
{
    constexpr auto interval_us = std::chrono::duration_cast<std::chrono::microseconds>( pull_report_interval ).count();
    if( std::fabs( sample.kg ) < pull_threshold_kg ||
        ( last_pull_us_ && sample.timestamp_us - *last_pull_us_ < static_cast<std::uint64_t>( interval_us ) ) ) {
        return;
    }
    last_pull_us_ = sample.timestamp_us;
    if( auto ret = channels::pull.publish( { .kg = sample.kg }, channels::lock_timeout ); !ret ) {
        LOG_ERR( "pull publish failed: %d", fw::err::to_zephyr( ret.error() ) );
    }
}

void Measurement::log_stats() const
{
    // One decimal in fixed point: the logs have no float formatting.
    auto const tenths = []( double value ) { return std::lround( value * 10.0 ); };
    auto const sps = tenths( stats_.samples_per_second() );
    LOG_INF( "measurement stopped: %u samples at %ld.%ld SPS, longest gap %u us, %u lost; %u batches dropped so far",
             static_cast<unsigned>( stats_.count() ), sps / 10, sps % 10, static_cast<unsigned>( stats_.max_gap_us() ),
             static_cast<unsigned>( load_cell_.lost_samples() ), static_cast<unsigned>( dropped_batches_ ) );
    if( stats_.count() == 0 ) {
        return;
    }
    auto const sd = tenths( stats_.sd_raw() );
    LOG_INF( "raw codes: mean %ld, sd %ld.%ld, min %d, max %d; mean %ld g", std::lround( stats_.mean_raw() ), sd / 10, sd % 10,
             static_cast<int>( stats_.min_raw() ), static_cast<int>( stats_.max_raw() ), std::lround( stats_.mean_kg() * 1000.0 ) );
}

void Measurement::publish_state() const
{
    if( auto ret = channels::measurement_state.publish( { .measuring = measuring() }, channels::lock_timeout ); !ret ) {
        LOG_ERR( "measurement state publish failed: %d", fw::err::to_zephyr( ret.error() ) );
    }
}

fw::Result<> Measurement::reply( const progressor::Packet &packet )
{
    return client_.send_progressor( packet.bytes() );
}

std::size_t Measurement::batch_capacity() const
{
    std::size_t const per_packet = progressor::samples_per_packet( client_.att_mtu() );
    if( !rate_ ) {
        return per_packet;
    }
    auto const in_max_duration = static_cast<std::size_t>( hal::samples_per_second( *rate_ ) ) * max_batch_duration.count() / 1000;
    return std::clamp<std::size_t>( in_max_duration, 1, per_packet );
}

} // namespace measurement_manager
