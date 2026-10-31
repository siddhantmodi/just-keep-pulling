/**
 * @file test_load_cell.cpp
 * @brief Unit tests for load_cell_svc::LoadCell with a mocked ADC and real settings storage.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "calibration.hpp"
#include "load_cell.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <expected>
#include <fw/settings.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <hal/mocks/mock_load_cell_adc.hpp>
#include <limits>
#include <system_error>

using ::hal::LoadCellSample;
using ::hal::mocks::MockLoadCellAdc;
using ::testing::_;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::Return;

namespace
{

// 0.109 g per code: 1 kg = 9174.3 codes.
constexpr double codes_per_kg = 1000.0 / 0.109;

class LoadCellTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE( fw::settings::init() );
        (void)fw::settings::erase( load_cell_svc::factor_key );
        // The mocked ADC reads raw_ with an increasing timestamp.
        ON_CALL( adc_, read( _ ) )
            .WillByDefault(
                [this]( std::chrono::microseconds )
                {
                    timestamp_us_ += 1000;
                    return fw::Result<LoadCellSample>{ LoadCellSample{ .raw = raw_, .timestamp_us = timestamp_us_ } };
                } );
    }

    void TearDown() override
    {
        (void)fw::settings::erase( load_cell_svc::factor_key );
    }

    void set_load( double kg, std::int32_t zero = 1000 )
    {
        raw_ = zero + static_cast<std::int32_t>( std::lround( kg * codes_per_kg ) );
    }

    NiceMock<MockLoadCellAdc> adc_;
    load_cell_svc::LoadCell load_cell_{ adc_ };
    std::int32_t raw_{ 0 };
    std::uint64_t timestamp_us_{ 0 };
};

TEST_F( LoadCellTest, InitUsesDefaultFactorWhenNoneSaved )
{
    ASSERT_TRUE( load_cell_.init() );

    EXPECT_FLOAT_EQ( load_cell_.calibration_factor(), calibration::default_factor );
}

TEST_F( LoadCellTest, InitLoadsSavedFactor )
{
    ASSERT_TRUE( fw::settings::save( load_cell_svc::factor_key, 0.2F ) );

    ASSERT_TRUE( load_cell_.init() );

    EXPECT_FLOAT_EQ( load_cell_.calibration_factor(), 0.2F );
}

TEST_F( LoadCellTest, InitIgnoresImplausibleSavedFactor )
{
    ASSERT_TRUE( fw::settings::save( load_cell_svc::factor_key, 5.0F ) );

    ASSERT_TRUE( load_cell_.init() );

    EXPECT_FLOAT_EQ( load_cell_.calibration_factor(), calibration::default_factor );
}

TEST_F( LoadCellTest, InitFailsWhenAdcNotReady )
{
    EXPECT_CALL( adc_, init() ).WillOnce( Return( fw::Result<>{ std::unexpected( std::errc::no_such_device ) } ) );

    EXPECT_EQ( load_cell_.init().error(), std::errc::no_such_device );
}

TEST_F( LoadCellTest, ReadAppliesTareAndFactorAndKeepsTimestamp )
{
    ASSERT_TRUE( load_cell_.init() );
    ASSERT_TRUE( load_cell_.start( 175 ) );
    set_load( 0.0 );
    ASSERT_TRUE( load_cell_.tare() );
    set_load( 20.0 );

    auto sample = load_cell_.read( std::chrono::milliseconds{ 10 } );

    ASSERT_TRUE( sample );
    EXPECT_NEAR( sample->kg, 20.0F, 0.001F );
    EXPECT_EQ( sample->timestamp_us, timestamp_us_ );
}

TEST_F( LoadCellTest, TareWhileStoppedSamplesBrieflyAtTheDefaultRate )
{
    ASSERT_TRUE( load_cell_.init() );
    InSequence sequence;
    EXPECT_CALL( adc_, start( load_cell_svc::default_samples_per_second ) );
    EXPECT_CALL( adc_, read( _ ) ).Times( 35 ); // 175 SPS x 200 ms
    EXPECT_CALL( adc_, stop() );

    ASSERT_TRUE( load_cell_.tare() );
}

TEST_F( LoadCellTest, TareWhileSamplingKeepsTheRunningRate )
{
    ASSERT_TRUE( load_cell_.init() );
    ASSERT_TRUE( load_cell_.start( 1000 ) );
    EXPECT_CALL( adc_, start( _ ) ).Times( 0 );
    EXPECT_CALL( adc_, stop() ).Times( 0 );
    EXPECT_CALL( adc_, read( _ ) ).Times( 200 ); // 1000 SPS x 200 ms

    ASSERT_TRUE( load_cell_.tare() );
}

TEST_F( LoadCellTest, CalibrationSamplesAt90SpsThenRestoresTheRunningRate )
{
    ASSERT_TRUE( load_cell_.init() );
    ASSERT_TRUE( load_cell_.start( 175 ) );
    InSequence sequence;
    EXPECT_CALL( adc_, start( 90 ) );
    EXPECT_CALL( adc_, read( _ ) ).Times( 112 ); // 90 SPS x 1.25 s
    EXPECT_CALL( adc_, start( 175 ) );

    EXPECT_EQ( load_cell_.add_calibration_point( 0.0F ), 1U );
}

TEST_F( LoadCellTest, TwoPointsFitAndSaveTheFactor )
{
    ASSERT_TRUE( load_cell_.init() );
    set_load( 0.0 );
    ASSERT_TRUE( load_cell_.add_calibration_point( 0.0F ) );
    set_load( 20.0 );

    auto points = load_cell_.add_calibration_point( 20.0F );

    ASSERT_TRUE( points );
    EXPECT_EQ( *points, 2U );
    EXPECT_NEAR( load_cell_.calibration_factor(), 0.109F, 1e-5F );
    auto saved = fw::settings::load<float>( load_cell_svc::factor_key );
    ASSERT_TRUE( saved );
    EXPECT_FLOAT_EQ( *saved, load_cell_.calibration_factor() );
}

TEST_F( LoadCellTest, FailedFitClearsPointsAndKeepsTheFactor )
{
    ASSERT_TRUE( load_cell_.init() );
    set_load( 0.0 );
    ASSERT_TRUE( load_cell_.add_calibration_point( 0.0F ) );

    // The load changed but the reading did not.
    EXPECT_EQ( load_cell_.add_calibration_point( 20.0F ).error(), std::errc::invalid_argument );

    EXPECT_FLOAT_EQ( load_cell_.calibration_factor(), calibration::default_factor );
    EXPECT_EQ( load_cell_.add_calibration_point( 0.0F ), 1U ); // starting over
}

TEST_F( LoadCellTest, RejectsInvalidCalibrationWeights )
{
    ASSERT_TRUE( load_cell_.init() );
    EXPECT_CALL( adc_, read( _ ) ).Times( 0 );

    EXPECT_EQ( load_cell_.add_calibration_point( -1.0F ).error(), std::errc::invalid_argument );
    EXPECT_EQ( load_cell_.add_calibration_point( std::numeric_limits<float>::quiet_NaN() ).error(), std::errc::invalid_argument );
    EXPECT_EQ( load_cell_.add_calibration_point( std::numeric_limits<float>::infinity() ).error(), std::errc::invalid_argument );
}

TEST_F( LoadCellTest, AcceptsAtMostTwentyPoints )
{
    ASSERT_TRUE( load_cell_.init() );
    for( int i = 0; i < 20; ++i ) {
        set_load( i );
        ASSERT_TRUE( load_cell_.add_calibration_point( static_cast<float>( i ) ) ) << "point " << i;
    }

    EXPECT_EQ( load_cell_.add_calibration_point( 25.0F ).error(), std::errc::no_buffer_space );
}

TEST_F( LoadCellTest, ResetCalibrationRestoresDefaultAndErasesSavedFactor )
{
    ASSERT_TRUE( fw::settings::save( load_cell_svc::factor_key, 0.2F ) );
    ASSERT_TRUE( load_cell_.init() );

    ASSERT_TRUE( load_cell_.reset_calibration() );

    EXPECT_FLOAT_EQ( load_cell_.calibration_factor(), calibration::default_factor );
    EXPECT_EQ( fw::settings::load<float>( load_cell_svc::factor_key ).error(), std::errc::no_such_file_or_directory );
}

TEST_F( LoadCellTest, AveragingFailureRestoresStoppedState )
{
    ASSERT_TRUE( load_cell_.init() );
    InSequence sequence;
    EXPECT_CALL( adc_, start( _ ) );
    EXPECT_CALL( adc_, read( _ ) ).WillOnce( Return( fw::Result<LoadCellSample>{ std::unexpected( std::errc::io_error ) } ) );
    EXPECT_CALL( adc_, stop() );

    EXPECT_EQ( load_cell_.tare().error(), std::errc::io_error );
}

TEST_F( LoadCellTest, ReadPassesAdcErrorsThrough )
{
    ASSERT_TRUE( load_cell_.init() );
    EXPECT_CALL( adc_, read( _ ) ).WillOnce( Return( fw::Result<LoadCellSample>{ std::unexpected( std::errc::operation_not_permitted ) } ) );

    EXPECT_EQ( load_cell_.read( std::chrono::milliseconds{ 1 } ).error(), std::errc::operation_not_permitted );
}

} // namespace
