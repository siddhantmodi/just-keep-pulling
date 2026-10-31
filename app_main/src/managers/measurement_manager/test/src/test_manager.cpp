/**
 * @file test_manager.cpp
 * @brief Unit tests of measurement_manager::Manager against a mocked load cell and client.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "manager.hpp"
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

Event command( Opcode opcode, float calibration_kg = 0.0F )
{
    return Event::from( progressor::Command{ .opcode = opcode, .calibration_kg = calibration_kg } );
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

class ManagerTest : public testing::Test
{
protected:
    ManagerTest()
    {
        ON_CALL( load_cell_, sample_rate() ).WillByDefault( Return( hal::SampleRate::sps175 ) );
        ON_CALL( load_cell_, start( _ ) ).WillByDefault( Return( fw::Result<>{} ) );
        ON_CALL( load_cell_, stop() ).WillByDefault( Return( fw::Result<>{} ) );
        ON_CALL( load_cell_, tare() ).WillByDefault( Return( fw::Result<>{} ) );
        ON_CALL( client_, att_mtu() ).WillByDefault( Return( large_mtu ) );
        ON_CALL( client_, set_streaming( _ ) ).WillByDefault( Return( fw::Result<>{} ) );
        ON_CALL( client_, disconnect() ).WillByDefault( Return( fw::Result<>{} ) );
        ON_CALL( client_, send( _ ) )
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
        ASSERT_TRUE( manager_.handle( command( Opcode::start_measurement ) ) );
    }

    void sample( unsigned count )
    {
        for( unsigned i = 0; i < count; ++i ) {
            ASSERT_TRUE( manager_.sample() );
        }
    }

    NiceMock<load_cell_svc::MockLoadCell> load_cell_;
    NiceMock<ble_svc::MockClient> client_;
    measurement_manager::Manager manager_{ load_cell_, client_, id };
    std::vector<std::vector<std::uint8_t>> sent_;
    float next_kg_{ 0.0F };
    std::uint64_t next_us_{ 12'345'678 };
    std::uint32_t period_us_{ 5714 };
};

TEST_F( ManagerTest, StartTaresTheScale )
{
    EXPECT_CALL( load_cell_, tare() ).WillOnce( Return( fw::Result<>{} ) );
    EXPECT_TRUE( manager_.start() );
}

TEST_F( ManagerTest, StartSucceedsWhenTheTareFails )
{
    EXPECT_CALL( load_cell_, tare() ).WillOnce( Return( std::unexpected( std::errc::timed_out ) ) );
    EXPECT_TRUE( manager_.start() );
}

TEST_F( ManagerTest, StartMeasurementSamplesAtTheConfiguredRateAndAsksForStreaming )
{
    ON_CALL( load_cell_, sample_rate() ).WillByDefault( Return( hal::SampleRate::sps600 ) );
    EXPECT_CALL( load_cell_, start( hal::SampleRate::sps600 ) ).WillOnce( Return( fw::Result<>{} ) );
    EXPECT_CALL( client_, set_streaming( true ) );

    EXPECT_TRUE( manager_.handle( command( Opcode::start_measurement ) ) );
    EXPECT_TRUE( manager_.measuring() );
}

TEST_F( ManagerTest, StartMeasurementFailsWhenTheLoadCellDoes )
{
    EXPECT_CALL( load_cell_, start( _ ) ).WillOnce( Return( std::unexpected( std::errc::io_error ) ) );
    EXPECT_CALL( client_, set_streaming( _ ) ).Times( 0 );

    EXPECT_EQ( manager_.handle( command( Opcode::start_measurement ) ), std::unexpected( std::errc::io_error ) );
    EXPECT_FALSE( manager_.measuring() );
}

TEST_F( ManagerTest, SampleFailsWhenNotMeasuring )
{
    EXPECT_CALL( load_cell_, read( _ ) ).Times( 0 );
    EXPECT_EQ( manager_.sample(), std::unexpected( std::errc::operation_not_permitted ) );
}

TEST_F( ManagerTest, BatchesUpTo50MillisecondsOfSamples )
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

TEST_F( ManagerTest, TimestampsCountFromOnePeriodBeforeTheFirstSample )
{
    start_measuring( hal::SampleRate::sps175 );
    sample( 8 );

    auto const samples = samples_in( sent_.at( 0 ) );
    for( std::size_t i = 0; i < samples.size(); ++i ) {
        EXPECT_EQ( samples[i].timestamp_us, ( i + 1 ) * 5714 );
    }
}

TEST_F( ManagerTest, BatchesAtMostWhatOneNotificationCarries )
{
    ON_CALL( client_, att_mtu() ).WillByDefault( Return( 23 ) ); // 2 samples
    start_measuring( hal::SampleRate::sps1000 );

    sample( 6 );

    ASSERT_EQ( sent_.size(), 3U );
    EXPECT_EQ( samples_in( sent_[0] ).size(), 2U );
}

TEST_F( ManagerTest, SendsThirtySamplesPerNotificationAt1000Sps )
{
    start_measuring( hal::SampleRate::sps1000 );

    sample( 30 );

    ASSERT_EQ( sent_.size(), 1U );
    EXPECT_EQ( samples_in( sent_[0] ).size(), 30U );
}

TEST_F( ManagerTest, SendsEverySampleAtLowRates )
{
    start_measuring( hal::SampleRate::sps20 ); // one sample per 50 ms

    sample( 3 );

    EXPECT_EQ( sent_.size(), 3U );
}

TEST_F( ManagerTest, UsesALargerMtuOnceExchanged )
{
    ON_CALL( client_, att_mtu() ).WillByDefault( Return( 23 ) );
    start_measuring( hal::SampleRate::sps1000 );
    sample( 2 );
    ON_CALL( client_, att_mtu() ).WillByDefault( Return( large_mtu ) );

    sample( 30 );

    ASSERT_EQ( sent_.size(), 2U );
    EXPECT_EQ( samples_in( sent_[1] ).size(), 30U );
}

TEST_F( ManagerTest, StopSendsThePendingSamplesAndStopsTheLoadCell )
{
    start_measuring();
    sample( 3 );
    EXPECT_CALL( load_cell_, stop() ).WillOnce( Return( fw::Result<>{} ) );
    EXPECT_CALL( client_, set_streaming( false ) );

    EXPECT_TRUE( manager_.handle( command( Opcode::stop_measurement ) ) );

    ASSERT_EQ( sent_.size(), 1U );
    EXPECT_EQ( samples_in( sent_[0] ).size(), 3U );
    EXPECT_FALSE( manager_.measuring() );
}

TEST_F( ManagerTest, StopWhenNotMeasuringDoesNothing )
{
    EXPECT_CALL( load_cell_, stop() ).Times( 0 );
    EXPECT_TRUE( manager_.handle( command( Opcode::stop_measurement ) ) );
    EXPECT_TRUE( sent_.empty() );
}

TEST_F( ManagerTest, StartWhileMeasuringStartsANewTimeline )
{
    start_measuring();
    sample( 3 );
    EXPECT_CALL( load_cell_, start( _ ) ).Times( 0 );

    ASSERT_TRUE( manager_.handle( command( Opcode::start_measurement ) ) );
    sample( 8 );

    ASSERT_EQ( sent_.size(), 1U );
    auto const samples = samples_in( sent_[0] );
    EXPECT_FLOAT_EQ( samples[0].kg, 3.0F ); // the first three were dropped
    EXPECT_EQ( samples[0].timestamp_us, 5714U );
}

TEST_F( ManagerTest, TareSendsThePendingSamplesFirst )
{
    start_measuring();
    sample( 2 );
    testing::InSequence const in_order;
    EXPECT_CALL( client_, send( _ ) );
    EXPECT_CALL( load_cell_, tare() ).WillOnce( Return( fw::Result<>{} ) );

    EXPECT_TRUE( manager_.handle( command( Opcode::tare ) ) );
}

TEST_F( ManagerTest, TareReportsAFailure )
{
    EXPECT_CALL( load_cell_, tare() ).WillOnce( Return( std::unexpected( std::errc::timed_out ) ) );
    EXPECT_EQ( manager_.handle( command( Opcode::tare ) ), std::unexpected( std::errc::timed_out ) );
}

TEST_F( ManagerTest, AddCalibrationPointPassesTheWeight )
{
    EXPECT_CALL( load_cell_, add_calibration_point( 20.5F ) ).WillOnce( Return( fw::Result<std::size_t>{ 1 } ) );
    EXPECT_TRUE( manager_.handle( command( Opcode::add_calibration_point, 20.5F ) ) );
    EXPECT_TRUE( sent_.empty() ); // no response, as Crimpdeq
}

TEST_F( ManagerTest, AddCalibrationPointReportsAFailure )
{
    EXPECT_CALL( load_cell_, add_calibration_point( _ ) ).WillOnce( Return( std::unexpected( std::errc::no_buffer_space ) ) );
    EXPECT_EQ( manager_.handle( command( Opcode::add_calibration_point, 1.0F ) ), std::unexpected( std::errc::no_buffer_space ) );
}

TEST_F( ManagerTest, AnswersTheAppVersion )
{
    ASSERT_TRUE( manager_.handle( command( Opcode::get_app_version ) ) );
    ASSERT_EQ( sent_.size(), 1U );
    EXPECT_EQ( sent_[0], ( std::vector<std::uint8_t>{ 0x00, 5, '2', '.', '0', '.', '4' } ) );
}

TEST_F( ManagerTest, AnswersTheProgressorIdLeastSignificantByteFirst )
{
    ASSERT_TRUE( manager_.handle( command( Opcode::get_progressor_id ) ) );
    ASSERT_EQ( sent_.size(), 1U );
    EXPECT_EQ( sent_[0], ( std::vector<std::uint8_t>{ 0x00, 6, 0xD5, 0x1B, 0x01, 0x17, 0x4E, 0x7E } ) );
}

TEST_F( ManagerTest, AnswersTheBatteryVoltage )
{
    ASSERT_TRUE( manager_.handle( command( Opcode::sample_battery ) ) );
    ASSERT_EQ( sent_.size(), 1U );
    EXPECT_EQ( sent_[0], ( std::vector<std::uint8_t>{ 0x00, 4, 0xA0, 0x0F, 0x00, 0x00 } ) ); // 4000 mV
}

TEST_F( ManagerTest, ShutdownStopsMeasuringAndDisconnects )
{
    start_measuring();
    sample( 2 );
    EXPECT_CALL( load_cell_, stop() ).WillOnce( Return( fw::Result<>{} ) );
    EXPECT_CALL( client_, disconnect() ).WillOnce( Return( fw::Result<>{} ) );

    EXPECT_TRUE( manager_.handle( command( Opcode::shutdown ) ) );
    EXPECT_FALSE( manager_.measuring() );
    EXPECT_TRUE( sent_.empty() );
}

TEST_F( ManagerTest, DisconnectionStopsMeasuringWithoutSending )
{
    start_measuring();
    sample( 2 );
    EXPECT_CALL( load_cell_, stop() ).WillOnce( Return( fw::Result<>{} ) );

    EXPECT_TRUE( manager_.handle( connection( false ) ) );
    EXPECT_FALSE( manager_.measuring() );
    EXPECT_TRUE( sent_.empty() );
}

TEST_F( ManagerTest, ConnectionChangesNothing )
{
    EXPECT_CALL( load_cell_, start( _ ) ).Times( 0 );
    EXPECT_TRUE( manager_.handle( connection( true ) ) );
    EXPECT_FALSE( manager_.measuring() );
}

TEST_F( ManagerTest, DropsBatchesTheLinkHasNoRoomForAndKeepsMeasuring )
{
    start_measuring( hal::SampleRate::sps20 );
    EXPECT_CALL( client_, send( _ ) ).WillOnce( Return( std::unexpected( std::errc::no_buffer_space ) ) ).WillRepeatedly( Return( fw::Result<>{} ) );

    sample( 3 );

    EXPECT_EQ( manager_.dropped_batches(), 1U );
    EXPECT_TRUE( manager_.measuring() );
}

TEST_F( ManagerTest, KeepsMeasuringWhenTheClientIsNotSubscribed )
{
    start_measuring( hal::SampleRate::sps20 );
    ON_CALL( client_, send( _ ) ).WillByDefault( Return( std::unexpected( std::errc::not_connected ) ) );

    sample( 3 );

    EXPECT_EQ( manager_.dropped_batches(), 0U );
    EXPECT_TRUE( manager_.measuring() );
}

TEST_F( ManagerTest, AFailedReadIsRetried )
{
    start_measuring();
    EXPECT_CALL( load_cell_, read( _ ) )
        .WillOnce( Return( std::unexpected( std::errc::resource_unavailable_try_again ) ) )
        .WillOnce( Return( fw::Result<load_cell_svc::ForceSample>{ { .kg = 1.0F, .timestamp_us = 1'000'000 } } ) );

    EXPECT_EQ( manager_.sample(), std::unexpected( std::errc::resource_unavailable_try_again ) );
    EXPECT_TRUE( manager_.sample() );
    EXPECT_TRUE( manager_.measuring() );
}

TEST_F( ManagerTest, RepeatedFailedReadsStopTheMeasurement )
{
    start_measuring();
    ON_CALL( load_cell_, read( _ ) ).WillByDefault( Return( std::unexpected( std::errc::resource_unavailable_try_again ) ) );
    EXPECT_CALL( load_cell_, stop() ).WillOnce( Return( fw::Result<>{} ) );

    for( unsigned i = 0; i < measurement_manager::max_read_failures; ++i ) {
        EXPECT_FALSE( manager_.sample() );
    }
    EXPECT_FALSE( manager_.measuring() );
}

TEST_F( ManagerTest, ReadsWaitAtLeastTwoSamplePeriods )
{
    start_measuring( hal::SampleRate::sps20 );
    EXPECT_CALL( load_cell_, read( testing::Ge( std::chrono::microseconds{ 100'000 } ) ) );
    (void)manager_.sample();
}

} // namespace
