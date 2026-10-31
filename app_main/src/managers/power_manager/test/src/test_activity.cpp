/**
 * @file test_activity.cpp
 * @brief Unit tests of power_manager::Activity against a mocked IMU and client.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "activity.hpp"
#include "mock_client.hpp"
#include "mock_led.hpp"
#include "mock_motion.hpp"

#include <chrono>
#include <expected>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <system_error>

namespace
{

using namespace std::chrono_literals;
using power_manager::activity_check_interval;
using progressor::Opcode;
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;

constexpr power_manager::IdleTimeouts timeouts{ .disconnected = 240s, .connected = 1800s };

class ActivityTest : public testing::Test
{
protected:
    ActivityTest()
    {
        ON_CALL( motion_, moved() ).WillByDefault( Return( false ) );
        EXPECT_CALL( motion_, moved() ).Times( testing::AnyNumber() );
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
    power_manager::Activity activity_{ motion_, client_, indicator_, timeouts };
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

TEST_F( ActivityTest, BatteryPollsDoNotKeepItAwake )
{
    idle_for( 200s );
    activity_.on_command( Opcode::sample_battery );
    EXPECT_EQ( activity_.idle(), 200s );
}

TEST_F( ActivityTest, NothingCountsWhileMeasuring )
{
    activity_.on_measurement( true );
    idle_for( 3600s ); // no sleep: StrictMock fails on any call
    EXPECT_EQ( activity_.idle(), 0s );

    activity_.on_measurement( false );
    idle_for( 10s );
    EXPECT_EQ( activity_.idle(), 10s );
}

TEST_F( ActivityTest, WaitsLongerWithAClient )
{
    activity_.on_client_connection( true );
    idle_for( 1790s ); // past the 240 s without a client
    EXPECT_EQ( activity_.idle(), 1790s );
}

TEST_F( ActivityTest, DisconnectsTheClientThenSleeps )
{
    activity_.on_client_connection( true );
    idle_for( 1790s );

    EXPECT_CALL( client_, disconnect() ).WillOnce( Return( fw::Result<>{} ) );
    ASSERT_TRUE( activity_.check() );

    activity_.on_client_connection( false ); // the disconnection asked for
    EXPECT_CALL( motion_, sleep_until_moved() ).WillOnce( Return( fw::Result<>{} ) );
    EXPECT_TRUE( activity_.check() );
}

TEST_F( ActivityTest, AReconnectionCancelsTheSleep )
{
    activity_.on_client_connection( true );
    idle_for( 1790s );
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
    idle_for( 1790s );
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
