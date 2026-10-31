/**
 * @file test_led.cpp
 * @brief Unit tests of led_svc::Led against three mocked digital outputs.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "led.hpp"

#include <expected>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <hal/mocks/mock_digital_output.hpp>
#include <system_error>

namespace
{

using led_svc::Color;
using testing::Return;

class LedTest : public testing::Test
{
protected:
    void expect( bool red, bool green, bool blue )
    {
        EXPECT_CALL( red_, set( red ) ).WillOnce( Return( fw::Result<>{} ) );
        EXPECT_CALL( green_, set( green ) ).WillOnce( Return( fw::Result<>{} ) );
        EXPECT_CALL( blue_, set( blue ) ).WillOnce( Return( fw::Result<>{} ) );
    }

    testing::StrictMock<hal::mocks::MockDigitalOutput> red_;
    testing::StrictMock<hal::mocks::MockDigitalOutput> green_;
    testing::StrictMock<hal::mocks::MockDigitalOutput> blue_;
    led_svc::Led led_{ red_, green_, blue_ };
};

TEST_F( LedTest, InitConfiguresAllThreeOff )
{
    EXPECT_CALL( red_, init( false ) ).WillOnce( Return( fw::Result<>{} ) );
    EXPECT_CALL( green_, init( false ) ).WillOnce( Return( fw::Result<>{} ) );
    EXPECT_CALL( blue_, init( false ) ).WillOnce( Return( fw::Result<>{} ) );

    EXPECT_TRUE( led_.init() );
    EXPECT_EQ( led_.color(), Color::off );
}

TEST_F( LedTest, InitFailsWhenAPinIsNotReady )
{
    EXPECT_CALL( red_, init( false ) ).WillOnce( Return( std::unexpected( std::errc::no_such_device ) ) );
    EXPECT_EQ( led_.init(), std::unexpected( std::errc::no_such_device ) );
}

TEST_F( LedTest, ShowsOneColorAtATime )
{
    expect( false, true, false );
    ASSERT_TRUE( led_.show( Color::green ) );
    EXPECT_EQ( led_.color(), Color::green );

    expect( false, false, true );
    ASSERT_TRUE( led_.show( Color::blue ) );

    expect( true, false, false );
    ASSERT_TRUE( led_.show( Color::red ) );
}

TEST_F( LedTest, OffTurnsAllOff )
{
    expect( false, false, false );
    ASSERT_TRUE( led_.show( Color::off ) );
    EXPECT_EQ( led_.color(), Color::off );
}

TEST_F( LedTest, SetsEveryPinEvenAfterAFailure )
{
    EXPECT_CALL( red_, set( false ) ).WillOnce( Return( std::unexpected( std::errc::io_error ) ) );
    EXPECT_CALL( green_, set( true ) ).WillOnce( Return( fw::Result<>{} ) );
    EXPECT_CALL( blue_, set( false ) ).WillOnce( Return( fw::Result<>{} ) );

    EXPECT_EQ( led_.show( Color::green ), std::unexpected( std::errc::io_error ) );
}

} // namespace
