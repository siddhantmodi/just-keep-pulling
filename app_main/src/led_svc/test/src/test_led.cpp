/**
 * @file test_led.cpp
 * @brief Unit tests for led_svc::Led with a mocked hal::DigitalOutput.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "led.hpp"

#include <expected>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <hal/mocks/mock_digital_output.hpp>
#include <system_error>

using ::hal::mocks::MockDigitalOutput;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrictMock;

namespace
{

const fw::Result<> io_error{ std::unexpected( std::errc::io_error ) };

class LedTest : public ::testing::Test
{
protected:
    NiceMock<MockDigitalOutput> output_;
    led_svc::Led<NiceMock<MockDigitalOutput>> led_{ output_ };
};

TEST_F( LedTest, InitDrivesLedOff )
{
    EXPECT_CALL( output_, init( false ) ).Times( 1 );

    ASSERT_TRUE( led_.init() );
    EXPECT_FALSE( led_.is_on() );
}

TEST_F( LedTest, InitPropagatesOutputError )
{
    EXPECT_CALL( output_, init( false ) ).WillOnce( Return( fw::Result<>{ std::unexpected( std::errc::no_such_device ) } ) );

    auto ret = led_.init();

    ASSERT_FALSE( ret );
    EXPECT_EQ( ret.error(), std::errc::no_such_device );
}

TEST_F( LedTest, SetAfterFailedInitIsRejected )
{
    EXPECT_CALL( output_, init( false ) ).WillOnce( Return( io_error ) );
    ASSERT_FALSE( led_.init() );
    EXPECT_CALL( output_, set( ::testing::_ ) ).Times( 0 );

    auto ret = led_.set( true );

    ASSERT_FALSE( ret );
    EXPECT_EQ( ret.error(), std::errc::operation_not_permitted );
}

TEST_F( LedTest, SetBeforeInitIsRejectedWithoutTouchingHardware )
{
    EXPECT_CALL( output_, set( ::testing::_ ) ).Times( 0 );

    auto ret = led_.set( true );

    ASSERT_FALSE( ret );
    EXPECT_EQ( ret.error(), std::errc::operation_not_permitted );
    EXPECT_FALSE( led_.is_on() );
}

TEST_F( LedTest, SetDrivesOutputAndTracksState )
{
    ASSERT_TRUE( led_.init() );

    EXPECT_CALL( output_, set( true ) ).Times( 1 );
    ASSERT_TRUE( led_.set( true ) );
    EXPECT_TRUE( led_.is_on() );

    EXPECT_CALL( output_, set( false ) ).Times( 1 );
    ASSERT_TRUE( led_.set( false ) );
    EXPECT_FALSE( led_.is_on() );
}

TEST_F( LedTest, SetFailureKeepsPreviousState )
{
    ASSERT_TRUE( led_.init() );
    EXPECT_CALL( output_, set( true ) ).WillOnce( Return( io_error ) );

    auto ret = led_.set( true );

    ASSERT_FALSE( ret );
    EXPECT_EQ( ret.error(), std::errc::io_error );
    EXPECT_FALSE( led_.is_on() );
}

TEST_F( LedTest, ToggleAlternatesState )
{
    ASSERT_TRUE( led_.init() );
    ::testing::InSequence seq;
    EXPECT_CALL( output_, set( true ) );
    EXPECT_CALL( output_, set( false ) );

    ASSERT_TRUE( led_.toggle() );
    EXPECT_TRUE( led_.is_on() );
    ASSERT_TRUE( led_.toggle() );
    EXPECT_FALSE( led_.is_on() );
}

TEST( LedStrictTest, InitOnlyConfiguresTheOutput )
{
    StrictMock<MockDigitalOutput> output;
    led_svc::Led<StrictMock<MockDigitalOutput>> led{ output };
    EXPECT_CALL( output, init( false ) ).WillOnce( Return( fw::Result<>{} ) );

    EXPECT_TRUE( led.init() );
}

} // namespace
