/**
 * @file test_measurement.cpp
 * @brief Unit tests of measurement_manager::Measurement against a mocked load cell and client.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "measurement.hpp"
#include "mock_client.hpp"
#include "mock_load_cell.hpp"

#include <bit>
#include <chrono>
#include <cstdint>
#include <expected>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <span>
#include <system_error>
#include <vector>

namespace
{

using measurement_manager::Event;
using progressor::Opcode;
using testing::_;
using testing::NiceMock;
using testing::Return;

constexpr progressor::ProgressorId id{ 0x7E, 0x4E, 0x17, 0x01, 0x1B, 0xD5 };
constexpr std::uint16_t large_mtu = 247;

Event command( Opcode opcode )
{
    return Event::from( progressor::Command{ .opcode = opcode } );
}

Event add_point( float kg )
{
    return Event::from(
        channels::ConfigCommand{ .type = channels::ConfigCommand::Type::add_calibration_point, .calibration_kg = kg, .samples_per_second = 0 } );
}

Event reset_calibration()
{
    return Event::from(
        channels::ConfigCommand{ .type = channels::ConfigCommand::Type::reset_calibration, .calibration_kg = 0.0F, .samples_per_second = 0 } );
}

Event set_rate( std::uint16_t samples_per_second )
{
    return Event::from( channels::ConfigCommand{
        .type = channels::ConfigCommand::Type::set_sample_rate, .calibration_kg = 0.0F, .samples_per_second = samples_per_second } );
}

channels::CalibrationState calibration_state()
{
    auto state = channels::calibration_state.read( K_NO_WAIT );
    EXPECT_TRUE( state );
    return state.value_or( channels::CalibrationState{} );
}

Event connection( bool connected )
{
    return Event::from( channels::ClientConnection{ .connected = connected } );
}

std::uint32_t read_u32( const std::vector<std::uint8_t> &bytes, std::size_t at )
{
    std::uint32_t value = 0;
    for( std::size_t i = 0; i < 4; ++i ) {
        value |= static_cast<std::uint32_t>( bytes.at( at + i ) ) << ( 8 * i );
    }
    return value;
}

/** @brief The samples in a weight measurement packet. */
std::vector<progressor::Sample> samples_in( const std::vector<std::uint8_t> &packet )
{
    EXPECT_EQ( packet.at( 0 ), 0x01 );
    EXPECT_EQ( packet.at( 1 ), packet.size() - 2 );
    std::vector<progressor::Sample> samples;
    for( std::size_t at = 2; at + 8 <= packet.size(); at += 8 ) {
        samples.push_back( { .kg = std::bit_cast<float>( read_u32( packet, at ) ), .timestamp_us = read_u32( packet, at + 4 ) } );
    }
    return samples;
}

class MeasurementTest : public testing::Test
{
protected:
    MeasurementTest()
    {
        ON_CALL( load_cell_, sample_rate() ).WillByDefault( Return( hal::SampleRate::sps175 ) );
        ON_CALL( load_cell_, start( _ ) ).WillByDefault( Return( fw::Result<>{} ) );
        ON_CALL( load_cell_, stop() ).WillByDefault( Return( fw::Result<>{} ) );
        ON_CALL( load_cell_, tare() ).WillByDefault( Return( fw::Result<>{} ) );
        ON_CALL( client_, att_mtu() ).WillByDefault( Return( large_mtu ) );
        ON_CALL( client_, set_streaming( _ ) ).WillByDefault( Return( fw::Result<>{} ) );
        ON_CALL( client_, disconnect() ).WillByDefault( Return( fw::Result<>{} ) );
        ON_CALL( client_, send_progressor( _ ) )
            .WillByDefault(
                [this]( std::span<const std::uint8_t> bytes )
                {
                    sent_.emplace_back( bytes.begin(), bytes.end() );
                    return fw::Result<>{};
                } );
        // Samples every period, from an arbitrary boot time.
        ON_CALL( load_cell_, read( _ ) )
            .WillByDefault(
                [this]( std::chrono::microseconds /*timeout*/ )
                {
                    auto const sample = load_cell_svc::ForceSample{ .kg = next_kg_, .timestamp_us = next_us_ };
                    next_kg_ += 1.0F;
                    next_us_ += period_us_;
                    return fw::Result<load_cell_svc::ForceSample>{ sample };
                } );
    }

    void start_measuring( hal::SampleRate rate = hal::SampleRate::sps175 )
    {
        ON_CALL( load_cell_, sample_rate() ).WillByDefault( Return( rate ) );
        period_us_ = 1'000'000U / hal::samples_per_second( rate );
        ASSERT_TRUE( measurement_.handle( command( Opcode::start_measurement ) ) );
    }

    void sample( unsigned count )
    {
        for( unsigned i = 0; i < count; ++i ) {
            ASSERT_TRUE( measurement_.sample() );
        }
    }

    NiceMock<load_cell_svc::MockLoadCell> load_cell_;
    NiceMock<ble_svc::MockClient> client_;
    measurement_manager::Measurement measurement_{ load_cell_, client_, id };
    std::vector<std::vector<std::uint8_t>> sent_;
    float next_kg_{ 0.0F };
    std::uint64_t next_us_{ 12'345'678 };
    std::uint32_t period_us_{ 5714 };
};

TEST_F( MeasurementTest, StartTaresTheScale )
{
    EXPECT_CALL( load_cell_, tare() ).WillOnce( Return( fw::Result<>{} ) );
    EXPECT_TRUE( measurement_.start() );
}

TEST_F( MeasurementTest, StartSucceedsWhenTheTareFails )
{
    EXPECT_CALL( load_cell_, tare() ).WillOnce( Return( std::unexpected( std::errc::timed_out ) ) );
    EXPECT_TRUE( measurement_.start() );
}

TEST_F( MeasurementTest, StartMeasurementSamplesAtTheConfiguredRateAndAsksForStreaming )
{
    ON_CALL( load_cell_, sample_rate() ).WillByDefault( Return( hal::SampleRate::sps600 ) );
    EXPECT_CALL( load_cell_, start( hal::SampleRate::sps600 ) ).WillOnce( Return( fw::Result<>{} ) );
    EXPECT_CALL( client_, set_streaming( true ) );

    EXPECT_TRUE( measurement_.handle( command( Opcode::start_measurement ) ) );
    EXPECT_TRUE( measurement_.measuring() );
}

TEST_F( MeasurementTest, StartMeasurementFailsWhenTheLoadCellDoes )
{
    EXPECT_CALL( load_cell_, start( _ ) ).WillOnce( Return( std::unexpected( std::errc::io_error ) ) );
    EXPECT_CALL( client_, set_streaming( _ ) ).Times( 0 );

    EXPECT_EQ( measurement_.handle( command( Opcode::start_measurement ) ), std::unexpected( std::errc::io_error ) );
    EXPECT_FALSE( measurement_.measuring() );
}

TEST_F( MeasurementTest, SampleFailsWhenNotMeasuring )
{
    EXPECT_CALL( load_cell_, read( _ ) ).Times( 0 );
    EXPECT_EQ( measurement_.sample(), std::unexpected( std::errc::operation_not_permitted ) );
}

TEST_F( MeasurementTest, BatchesUpTo50MillisecondsOfSamples )
{
    start_measuring( hal::SampleRate::sps175 ); // 8 samples in 50 ms

    sample( 7 );
    EXPECT_TRUE( sent_.empty() );
    sample( 1 );

    ASSERT_EQ( sent_.size(), 1U );
    auto const samples = samples_in( sent_[0] );
    ASSERT_EQ( samples.size(), 8U );
    EXPECT_FLOAT_EQ( samples[0].kg, 0.0F );
    EXPECT_FLOAT_EQ( samples[7].kg, 7.0F );
}

TEST_F( MeasurementTest, TimestampsCountFromOnePeriodBeforeTheFirstSample )
{
    start_measuring( hal::SampleRate::sps175 );
    sample( 8 );

    auto const samples = samples_in( sent_.at( 0 ) );
    for( std::size_t i = 0; i < samples.size(); ++i ) {
        EXPECT_EQ( samples[i].timestamp_us, ( i + 1 ) * 5714 );
    }
}

TEST_F( MeasurementTest, BatchesAtMostWhatOneNotificationCarries )
{
    ON_CALL( client_, att_mtu() ).WillByDefault( Return( 23 ) ); // 2 samples
    start_measuring( hal::SampleRate::sps1000 );

    sample( 6 );

    ASSERT_EQ( sent_.size(), 3U );
    EXPECT_EQ( samples_in( sent_[0] ).size(), 2U );
}

TEST_F( MeasurementTest, SendsThirtySamplesPerNotificationAt1000Sps )
{
    start_measuring( hal::SampleRate::sps1000 );

    sample( 30 );

    ASSERT_EQ( sent_.size(), 1U );
    EXPECT_EQ( samples_in( sent_[0] ).size(), 30U );
}

TEST_F( MeasurementTest, SendsEverySampleAtLowRates )
{
    start_measuring( hal::SampleRate::sps20 ); // one sample per 50 ms

    sample( 3 );

    EXPECT_EQ( sent_.size(), 3U );
}

TEST_F( MeasurementTest, UsesALargerMtuOnceExchanged )
{
    ON_CALL( client_, att_mtu() ).WillByDefault( Return( 23 ) );
    start_measuring( hal::SampleRate::sps1000 );
    sample( 2 );
    ON_CALL( client_, att_mtu() ).WillByDefault( Return( large_mtu ) );

    sample( 30 );

    ASSERT_EQ( sent_.size(), 2U );
    EXPECT_EQ( samples_in( sent_[1] ).size(), 30U );
}

TEST_F( MeasurementTest, StopSendsThePendingSamplesAndStopsTheLoadCell )
{
    start_measuring();
    sample( 3 );
    EXPECT_CALL( load_cell_, stop() ).WillOnce( Return( fw::Result<>{} ) );
    EXPECT_CALL( client_, set_streaming( false ) );

    EXPECT_TRUE( measurement_.handle( command( Opcode::stop_measurement ) ) );

    ASSERT_EQ( sent_.size(), 1U );
    EXPECT_EQ( samples_in( sent_[0] ).size(), 3U );
    EXPECT_FALSE( measurement_.measuring() );
}

TEST_F( MeasurementTest, PublishesWhenAMeasurementStartsAndStops )
{
    start_measuring();
    auto state = channels::measurement_state.read( K_NO_WAIT );
    ASSERT_TRUE( state );
    EXPECT_TRUE( state->measuring );

    ASSERT_TRUE( measurement_.handle( command( Opcode::stop_measurement ) ) );
    state = channels::measurement_state.read( K_NO_WAIT );
    ASSERT_TRUE( state );
    EXPECT_FALSE( state->measuring );
}

TEST_F( MeasurementTest, StopWhenNotMeasuringDoesNothing )
{
    EXPECT_CALL( load_cell_, stop() ).Times( 0 );
    EXPECT_TRUE( measurement_.handle( command( Opcode::stop_measurement ) ) );
    EXPECT_TRUE( sent_.empty() );
}

TEST_F( MeasurementTest, StartWhileMeasuringStartsANewTimeline )
{
    start_measuring();
    sample( 3 );
    EXPECT_CALL( load_cell_, start( _ ) ).Times( 0 );

    ASSERT_TRUE( measurement_.handle( command( Opcode::start_measurement ) ) );
    sample( 8 );

    ASSERT_EQ( sent_.size(), 1U );
    auto const samples = samples_in( sent_[0] );
    EXPECT_FLOAT_EQ( samples[0].kg, 3.0F ); // the first three were dropped
    EXPECT_EQ( samples[0].timestamp_us, 5714U );
}

TEST_F( MeasurementTest, ReportsAPullAtMostEveryTenSeconds )
{
    start_measuring( hal::SampleRate::sps20 );
    ASSERT_TRUE( channels::pull.publish( { .kg = 0.0F }, K_NO_WAIT ) );
    next_kg_ = -25.0F; // pulls read either sign; this test only needs a large load
    auto const pulled = [] { return channels::pull.read( K_NO_WAIT ).value_or( channels::Pull{} ).kg; };

    sample( 1 );
    EXPECT_FLOAT_EQ( pulled(), -25.0F );

    ASSERT_TRUE( channels::pull.publish( { .kg = 0.0F }, K_NO_WAIT ) );
    sample( 199 ); // 9.95 s later
    EXPECT_FLOAT_EQ( pulled(), 0.0F );
    sample( 1 ); // 10 s
    EXPECT_NE( pulled(), 0.0F );
}

TEST_F( MeasurementTest, ALightLoadIsNotAPull )
{
    start_measuring();
    ASSERT_TRUE( channels::pull.publish( { .kg = 0.0F }, K_NO_WAIT ) );
    ON_CALL( load_cell_, read( _ ) ).WillByDefault( Return( fw::Result<load_cell_svc::ForceSample>{ { .kg = 1.9F, .timestamp_us = 1 } } ) );

    sample( 3 );

    EXPECT_FLOAT_EQ( channels::pull.read( K_NO_WAIT ).value_or( channels::Pull{ .kg = 99.0F } ).kg, 0.0F );
}

TEST_F( MeasurementTest, TareSendsThePendingSamplesFirst )
{
    start_measuring();
    sample( 2 );
    testing::InSequence const in_order;
    EXPECT_CALL( client_, send_progressor( _ ) );
    EXPECT_CALL( load_cell_, tare() ).WillOnce( Return( fw::Result<>{} ) );

    EXPECT_TRUE( measurement_.handle( command( Opcode::tare ) ) );
}

TEST_F( MeasurementTest, TareReportsAFailure )
{
    EXPECT_CALL( load_cell_, tare() ).WillOnce( Return( std::unexpected( std::errc::timed_out ) ) );
    EXPECT_EQ( measurement_.handle( command( Opcode::tare ) ), std::unexpected( std::errc::timed_out ) );
}

TEST_F( MeasurementTest, StartPublishesTheCalibration )
{
    ON_CALL( load_cell_, calibration_factor() ).WillByDefault( Return( 0.109F ) );
    ASSERT_TRUE( measurement_.start() );

    auto const state = calibration_state();
    EXPECT_EQ( state.status, channels::CalibrationStatus::ok );
    EXPECT_EQ( state.points, 0 );
    EXPECT_FLOAT_EQ( state.factor_g_per_code, 0.109F );
}

TEST_F( MeasurementTest, AddsACalibrationPointAndPublishesTheResult )
{
    ON_CALL( load_cell_, calibration_factor() ).WillByDefault( Return( 0.12F ) );
    ON_CALL( load_cell_, calibration_points() ).WillByDefault( Return( 2U ) );
    EXPECT_CALL( load_cell_, add_calibration_point( 20.5F ) ).WillOnce( Return( fw::Result<std::size_t>{ 2 } ) );

    EXPECT_TRUE( measurement_.handle( add_point( 20.5F ) ) );

    auto const state = calibration_state();
    EXPECT_EQ( state.status, channels::CalibrationStatus::ok );
    EXPECT_EQ( state.points, 2 );
    EXPECT_FLOAT_EQ( state.factor_g_per_code, 0.12F );
    EXPECT_TRUE( sent_.empty() ); // nothing on the Progressor service
}

TEST_F( MeasurementTest, PublishesMeasuringWhileAveragingAPoint )
{
    EXPECT_CALL( load_cell_, add_calibration_point( _ ) )
        .WillOnce(
            []
            {
                EXPECT_EQ( calibration_state().status, channels::CalibrationStatus::measuring );
                return fw::Result<std::size_t>{ 1 };
            } );
    EXPECT_TRUE( measurement_.handle( add_point( 5.0F ) ) );
}

TEST_F( MeasurementTest, ReportsAFullCalibration )
{
    EXPECT_CALL( load_cell_, add_calibration_point( _ ) ).WillOnce( Return( std::unexpected( std::errc::no_buffer_space ) ) );
    EXPECT_EQ( measurement_.handle( add_point( 1.0F ) ), std::unexpected( std::errc::no_buffer_space ) );
    EXPECT_EQ( calibration_state().status, channels::CalibrationStatus::full );
}

TEST_F( MeasurementTest, ARejectedFitClearsThePoints )
{
    EXPECT_CALL( load_cell_, add_calibration_point( _ ) )
        .WillOnce( Return( fw::Result<std::size_t>{ 1 } ) )
        .WillOnce( Return( std::unexpected( std::errc::result_out_of_range ) ) );
    ASSERT_TRUE( measurement_.handle( add_point( 0.0F ) ) );

    EXPECT_FALSE( measurement_.handle( add_point( 1.0F ) ) );

    auto const state = calibration_state();
    EXPECT_EQ( state.status, channels::CalibrationStatus::fit_rejected );
    EXPECT_EQ( state.points, 0 );
}

TEST_F( MeasurementTest, ReportsAFailedPoint )
{
    EXPECT_CALL( load_cell_, add_calibration_point( _ ) ).WillOnce( Return( std::unexpected( std::errc::timed_out ) ) );
    EXPECT_FALSE( measurement_.handle( add_point( 1.0F ) ) );
    EXPECT_EQ( calibration_state().status, channels::CalibrationStatus::failed );
}

TEST_F( MeasurementTest, ResetsTheCalibration )
{
    EXPECT_CALL( load_cell_, add_calibration_point( _ ) ).WillOnce( Return( fw::Result<std::size_t>{ 3 } ) );
    ASSERT_TRUE( measurement_.handle( add_point( 1.0F ) ) );
    EXPECT_CALL( load_cell_, reset_calibration() ).WillOnce( Return( fw::Result<>{} ) );
    ON_CALL( load_cell_, calibration_factor() ).WillByDefault( Return( 0.109F ) );

    EXPECT_TRUE( measurement_.handle( reset_calibration() ) );

    auto const state = calibration_state();
    EXPECT_EQ( state.status, channels::CalibrationStatus::ok );
    EXPECT_EQ( state.points, 0 );
    EXPECT_FLOAT_EQ( state.factor_g_per_code, 0.109F );
}

TEST_F( MeasurementTest, SetsTheSampleRate )
{
    EXPECT_CALL( load_cell_, set_sample_rate( hal::SampleRate::sps600 ) ).WillOnce( Return( fw::Result<>{} ) );
    EXPECT_TRUE( measurement_.handle( set_rate( 600 ) ) );
}

TEST_F( MeasurementTest, RejectsAnUnsupportedSampleRate )
{
    EXPECT_CALL( load_cell_, set_sample_rate( _ ) ).Times( 0 );
    EXPECT_EQ( measurement_.handle( set_rate( 100 ) ), std::unexpected( std::errc::not_supported ) );
}

TEST_F( MeasurementTest, AnswersTheAppVersion )
{
    ASSERT_TRUE( measurement_.handle( command( Opcode::get_app_version ) ) );
    ASSERT_EQ( sent_.size(), 1U );
    EXPECT_EQ( sent_[0], ( std::vector<std::uint8_t>{ 0x00, 5, '2', '.', '0', '.', '4' } ) );
}

TEST_F( MeasurementTest, AnswersTheProgressorIdLeastSignificantByteFirst )
{
    ASSERT_TRUE( measurement_.handle( command( Opcode::get_progressor_id ) ) );
    ASSERT_EQ( sent_.size(), 1U );
    EXPECT_EQ( sent_[0], ( std::vector<std::uint8_t>{ 0x00, 6, 0xD5, 0x1B, 0x01, 0x17, 0x4E, 0x7E } ) );
}

TEST_F( MeasurementTest, AnswersTheLatestBatteryReading )
{
    ASSERT_TRUE( channels::battery.publish( { .status = { .millivolts = 3812 }, .percent = 50 }, K_NO_WAIT ) );

    ASSERT_TRUE( measurement_.handle( command( Opcode::sample_battery ) ) );
    ASSERT_EQ( sent_.size(), 1U );
    EXPECT_EQ( sent_[0], ( std::vector<std::uint8_t>{ 0x00, 4, 0xE4, 0x0E, 0x00, 0x00 } ) ); // 3812 mV
}

TEST_F( MeasurementTest, ShutdownStopsMeasuringAndDisconnects )
{
    start_measuring();
    sample( 2 );
    EXPECT_CALL( load_cell_, stop() ).WillOnce( Return( fw::Result<>{} ) );
    EXPECT_CALL( client_, disconnect() ).WillOnce( Return( fw::Result<>{} ) );

    EXPECT_TRUE( measurement_.handle( command( Opcode::shutdown ) ) );
    EXPECT_FALSE( measurement_.measuring() );
    EXPECT_TRUE( sent_.empty() );
}

TEST_F( MeasurementTest, ShutdownDisconnectsEvenIfTheLoadCellFailsToStop )
{
    start_measuring();
    EXPECT_CALL( load_cell_, stop() ).WillOnce( Return( std::unexpected( std::errc::io_error ) ) );
    EXPECT_CALL( client_, disconnect() ).WillOnce( Return( fw::Result<>{} ) );

    EXPECT_EQ( measurement_.handle( command( Opcode::shutdown ) ), std::unexpected( std::errc::io_error ) );
    EXPECT_FALSE( measurement_.measuring() );
}

TEST_F( MeasurementTest, DisconnectionStopsMeasuringWithoutSending )
{
    start_measuring();
    sample( 2 );
    EXPECT_CALL( load_cell_, stop() ).WillOnce( Return( fw::Result<>{} ) );

    EXPECT_TRUE( measurement_.handle( connection( false ) ) );
    EXPECT_FALSE( measurement_.measuring() );
    EXPECT_TRUE( sent_.empty() );
}

TEST_F( MeasurementTest, ConnectionChangesNothing )
{
    EXPECT_CALL( load_cell_, start( _ ) ).Times( 0 );
    EXPECT_TRUE( measurement_.handle( connection( true ) ) );
    EXPECT_FALSE( measurement_.measuring() );
}

TEST_F( MeasurementTest, DropsBatchesTheLinkHasNoRoomForAndKeepsMeasuring )
{
    start_measuring( hal::SampleRate::sps20 );
    EXPECT_CALL( client_, send_progressor( _ ) )
        .WillOnce( Return( std::unexpected( std::errc::no_buffer_space ) ) )
        .WillRepeatedly( Return( fw::Result<>{} ) );

    sample( 3 );

    EXPECT_EQ( measurement_.dropped_batches(), 1U );
    EXPECT_TRUE( measurement_.measuring() );
}

TEST_F( MeasurementTest, KeepsMeasuringWhenTheClientIsNotSubscribed )
{
    start_measuring( hal::SampleRate::sps20 );
    ON_CALL( client_, send_progressor( _ ) ).WillByDefault( Return( std::unexpected( std::errc::not_connected ) ) );

    sample( 3 );

    EXPECT_EQ( measurement_.dropped_batches(), 0U );
    EXPECT_TRUE( measurement_.measuring() );
}

TEST_F( MeasurementTest, AFailedReadIsRetried )
{
    start_measuring();
    EXPECT_CALL( load_cell_, read( _ ) )
        .WillOnce( Return( std::unexpected( std::errc::resource_unavailable_try_again ) ) )
        .WillOnce( Return( fw::Result<load_cell_svc::ForceSample>{ { .kg = 1.0F, .timestamp_us = 1'000'000 } } ) );

    EXPECT_EQ( measurement_.sample(), std::unexpected( std::errc::resource_unavailable_try_again ) );
    EXPECT_TRUE( measurement_.sample() );
    EXPECT_TRUE( measurement_.measuring() );
}

TEST_F( MeasurementTest, RepeatedFailedReadsStopTheMeasurement )
{
    start_measuring();
    ON_CALL( load_cell_, read( _ ) ).WillByDefault( Return( std::unexpected( std::errc::resource_unavailable_try_again ) ) );
    EXPECT_CALL( load_cell_, stop() ).WillOnce( Return( fw::Result<>{} ) );

    for( unsigned i = 0; i < measurement_manager::max_read_failures; ++i ) {
        EXPECT_FALSE( measurement_.sample() );
    }
    EXPECT_FALSE( measurement_.measuring() );
}

TEST_F( MeasurementTest, ReadsWaitAtLeastTwoSamplePeriods )
{
    start_measuring( hal::SampleRate::sps20 );
    EXPECT_CALL( load_cell_, read( testing::Ge( std::chrono::microseconds{ 100'000 } ) ) );
    (void)measurement_.sample();
}

} // namespace
