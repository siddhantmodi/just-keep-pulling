/**
 * @file measurement.cpp
 * @brief measurement_manager::Measurement implementation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "measurement.hpp"

#include <zephyr/logging/log.h>

#include <algorithm>
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

/** @brief Long enough for the next conversion: two periods plus a margin for scheduling. */
std::chrono::microseconds read_timeout( hal::SampleRate rate )
{
    return std::chrono::microseconds{ ( 2 * period_us( rate ) ) + 20'000 };
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
    return {};
}

fw::Result<> Measurement::handle( const Event &event )
{
    switch( event.type ) {
    case Event::Type::command:
        return on_command( event.command );
    case Event::Type::client_connection:
        return on_client_connection( event.client_connection );
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
    case Opcode::add_calibration_point:
    {
        send_batch();
        auto points = load_cell_.add_calibration_point( command.calibration_kg );
        if( !points ) {
            return std::unexpected( points.error() );
        }
        return {};
    }
    case Opcode::get_app_version:
        return reply( progressor::encode_app_version( app_version ) );
    case Opcode::shutdown:
        // As Crimpdeq: end the session and keep advertising. Power-off is the switch.
        LOG_INF( "shutdown requested, disconnecting" );
        if( auto stopped = stop_measurement( false ); !stopped ) {
            return stopped;
        }
        return client_.disconnect();
    case Opcode::sample_battery:
    {
        // The power manager's latest reading: measuring here would compete with it for the PMIC.
        auto battery = channels::battery.read();
        if( !battery ) {
            return std::unexpected( battery.error() );
        }
        return reply( progressor::encode_battery_voltage( battery->millivolts ) );
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

fw::Result<> Measurement::start_measurement()
{
    if( measuring() ) {
        // A new measurement on a new timeline, as Crimpdeq: the old one's pending samples are dropped.
        batch_size_ = 0;
        origin_us_.reset();
        return {};
    }

    hal::SampleRate const rate = load_cell_.sample_rate();
    if( auto ret = load_cell_.start( rate ); !ret ) {
        LOG_ERR( "load cell start failed: %d", fw::err::to_zephyr( ret.error() ) );
        return ret;
    }
    rate_ = rate;
    origin_us_.reset();
    batch_size_ = 0;
    read_failures_ = 0;
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

    auto stopped = load_cell_.stop();
    LOG_INF( "measurement stopped: %u samples lost, %u batches dropped so far", static_cast<unsigned>( load_cell_.lost_samples() ),
             static_cast<unsigned>( dropped_batches_ ) );
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
    auto sample = load_cell_.read( read_timeout( *rate_ ) );
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
    auto sent = packet ? client_.send( packet->bytes() ) : std::unexpected( packet.error() );
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

fw::Result<> Measurement::reply( const progressor::Packet &packet )
{
    return client_.send( packet.bytes() );
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
