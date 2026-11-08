/**
 * @file test_activity.cpp
 * @brief Unit tests of power_manager::Activity against a mocked IMU, client and fuel gauge.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "activity.hpp"
#include "mock_client.hpp"
#include "mock_led.hpp"
#include "mock_motion.hpp"

#include <chrono>
#include <expected>
#include <fw/settings.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <hal/mocks/mock_fuel_gauge.hpp>
#include <system_error>

namespace
{

using namespace std::chrono_literals;
using power_manager::activity_check_interval;
using progressor::Opcode;
using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;

constexpr power_manager::IdleTimeouts timeouts{ .disconnected = 240s, .connected = 600s };

class ActivityTest : public testing::Test
{
protected:
    ActivityTest()
    {
        ON_CALL( motion_, moved() ).WillByDefault( Return( false ) );
        EXPECT_CALL( motion_, moved() ).Times( testing::AnyNumber() );
    }

    void SetUp() override
    {
        ASSERT_TRUE( fw::settings::init() );
    }

    /** @brief Run the checks that fit in @p duration, all expected to succeed. */
    void idle_for( std::chrono::seconds duration )
    {
        for( auto elapsed = 0s; elapsed < duration; elapsed += activity_check_interval ) {
            ASSERT_TRUE( activity_.check() );
        }
    }

    StrictMock<motion_svc::MockMotion> motion_;
    StrictMock<ble_svc::MockClient> client_;
    NiceMock<led_svc::MockLed> led_;
    power_manager::Indicator indicator_{ led_ };
    NiceMock<hal::mocks::MockFuelGauge> fuel_;
    power_manager::Gauge gauge_{ fuel_ };
    power_manager::Activity activity_{ motion_, client_, indicator_, gauge_, timeouts };
};

TEST_F( ActivityTest, CountsIdleTime )
{
    idle_for( 60s );
    EXPECT_EQ( activity_.idle(), 60s );
}

TEST_F( ActivityTest, SleepsAfterTheTimeoutWithoutAClient )
{
    idle_for( 230s );
    EXPECT_CALL( motion_, sleep_until_moved() ).WillOnce( Return( fw::Result<>{} ) );
    EXPECT_TRUE( activity_.check() );
}

TEST_F( ActivityTest, SavesTheGaugeBeforeSleeping )
{
    ON_CALL( fuel_, state_size() ).WillByDefault( Return( 4 ) );
    ASSERT_TRUE( gauge_.update( {}, 0s ) ); // started, so there is a state to save
    idle_for( 230s );
    testing::InSequence const in_order;
    EXPECT_CALL( fuel_, save_state( _ ) ).WillOnce( Return( fw::Result<>{} ) );
    EXPECT_CALL( motion_, sleep_until_moved() ).WillOnce( Return( fw::Result<>{} ) );
    EXPECT_TRUE( activity_.check() );
}

TEST_F( ActivityTest, TurnsTheLedOffToSleepAndBackOnIfItCannot )
{
    idle_for( 230s );
    ON_CALL( led_, color() ).WillByDefault( Return( led_svc::Color::green ) ); // so both writes are needed
    testing::InSequence const in_order;
    EXPECT_CALL( led_, show( led_svc::Color::off ) ).WillOnce( Return( fw::Result<>{} ) );
    EXPECT_CALL( motion_, sleep_until_moved() ).WillOnce( Return( std::unexpected( std::errc::not_supported ) ) );
    EXPECT_CALL( led_, show( led_svc::Color::blue ) ).WillOnce( Return( fw::Result<>{} ) );

    EXPECT_FALSE( activity_.check() );
}

TEST_F( ActivityTest, MotionKeepsItAwake )
{
    idle_for( 200s );
    EXPECT_CALL( motion_, moved() ).WillOnce( Return( true ) );
    ASSERT_TRUE( activity_.check() );
    EXPECT_EQ( activity_.idle(), 0s );
}

TEST_F( ActivityTest, CommandsKeepItAwake )
{
    idle_for( 200s );
    activity_.on_command( Opcode::tare );
    EXPECT_EQ( activity_.idle(), 0s );
}

TEST_F( ActivityTest, ConfigurationKeepsItAwake )
{
    idle_for( 200s );
    activity_.on_configuration();
    EXPECT_EQ( activity_.idle(), 0s );
}

TEST_F( ActivityTest, BatteryPollsDoNotKeepItAwake )
{
    idle_for( 200s );
    activity_.on_command( Opcode::sample_battery );
    EXPECT_EQ( activity_.idle(), 200s );
}

TEST_F( ActivityTest, AMeasurementStartingOrStoppingKeepsItAwake )
{
    idle_for( 200s );
    activity_.on_measurement();
    EXPECT_EQ( activity_.idle(), 0s );
}

TEST_F( ActivityTest, PullsKeepItAwake )
{
    activity_.on_client_connection( true );
    activity_.on_measurement();
    for( int minute = 0; minute < 60; ++minute ) {
        idle_for( 60s );
        activity_.on_pull();
    }
    EXPECT_EQ( activity_.idle(), 0s ); // an hour of pulls: no disconnect (StrictMock fails on any call)
}

TEST_F( ActivityTest, AMeasurementNobodyPullsOnIdlesLikeAConnection )
{
    activity_.on_client_connection( true );
    activity_.on_measurement();
    idle_for( 590s );
    EXPECT_CALL( client_, disconnect() ).WillOnce( Return( fw::Result<>{} ) );

    idle_for( 10s );
}

TEST_F( ActivityTest, WaitsLongerWithAClient )
{
    activity_.on_client_connection( true );
    idle_for( 590s ); // past the 240 s without a client
    EXPECT_EQ( activity_.idle(), 590s );
}

TEST_F( ActivityTest, DisconnectsTheClientThenSleeps )
{
    activity_.on_client_connection( true );
    idle_for( 590s );

    EXPECT_CALL( client_, disconnect() ).WillOnce( Return( fw::Result<>{} ) );
    ASSERT_TRUE( activity_.check() );

    activity_.on_client_connection( false ); // the disconnection asked for
    EXPECT_CALL( motion_, sleep_until_moved() ).WillOnce( Return( fw::Result<>{} ) );
    EXPECT_TRUE( activity_.check() );
}

TEST_F( ActivityTest, AReconnectionCancelsTheSleep )
{
    activity_.on_client_connection( true );
    idle_for( 590s );
    EXPECT_CALL( client_, disconnect() ).WillOnce( Return( fw::Result<>{} ) );
    ASSERT_TRUE( activity_.check() );
    activity_.on_client_connection( false );

    activity_.on_client_connection( true );
    ASSERT_TRUE( activity_.check() ); // no sleep
    EXPECT_EQ( activity_.idle(), activity_check_interval );
}

TEST_F( ActivityTest, MotionWhileDisconnectingCancelsTheSleep )
{
    activity_.on_client_connection( true );
    idle_for( 590s );
    EXPECT_CALL( client_, disconnect() ).WillOnce( Return( fw::Result<>{} ) );
    ASSERT_TRUE( activity_.check() );

    EXPECT_CALL( motion_, moved() ).WillOnce( Return( true ) );
    ASSERT_TRUE( activity_.check() ); // no sleep
    EXPECT_EQ( activity_.idle(), 0s );
}

TEST_F( ActivityTest, AFailedSleepStartsCountingAgain )
{
    idle_for( 230s );
    EXPECT_CALL( motion_, sleep_until_moved() ).WillOnce( Return( std::unexpected( std::errc::not_supported ) ) );

    EXPECT_EQ( activity_.check(), std::unexpected( std::errc::not_supported ) );
    EXPECT_EQ( activity_.idle(), 0s );
}

} // namespace
