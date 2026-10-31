/**
 * @file test_app.cpp
 * @brief Unit tests for app_manager::App with a mocked LED and real zbus channels.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "app.hpp"
#include "channels.hpp"
#include "mock_led.hpp"

#include <expected>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <system_error>

using ::led_svc::MockLed;
using ::testing::NiceMock;
using ::testing::Return;

namespace
{

bool published_led_state()
{
    auto state = channels::led_state.read();
    EXPECT_TRUE( state );
    return state && state->on;
}

class AppTest : public ::testing::Test
{
protected:
    NiceMock<MockLed> led_;
    app_manager::App app_{ led_ };
};

TEST_F( AppTest, StartPublishesCurrentLedState )
{
    ON_CALL( led_, is_on() ).WillByDefault( Return( true ) );

    ASSERT_TRUE( app_.start() );
    EXPECT_TRUE( published_led_state() );
}

TEST_F( AppTest, LedRequestDrivesLedAndPublishesState )
{
    EXPECT_CALL( led_, set( true ) ).WillOnce( Return( fw::Result<>{} ) );
    ON_CALL( led_, is_on() ).WillByDefault( Return( true ) );

    ASSERT_TRUE( app_.handle( app_manager::Event::from( { .on = true } ) ) );
    EXPECT_TRUE( published_led_state() );
}

TEST_F( AppTest, FailedLedRequestPublishesUnchangedState )
{
    ASSERT_TRUE( channels::led_state.publish( { .on = true } ) ); // Something stale the manager must overwrite.
    EXPECT_CALL( led_, set( true ) ).WillOnce( Return( fw::Result<>{ std::unexpected( std::errc::io_error ) } ) );
    ON_CALL( led_, is_on() ).WillByDefault( Return( false ) );

    auto ret = app_.handle( app_manager::Event::from( { .on = true } ) );

    ASSERT_FALSE( ret );
    EXPECT_EQ( ret.error(), std::errc::io_error );
    EXPECT_FALSE( published_led_state() );
}

TEST( EventTest, FromLedRequestCarriesPayload )
{
    auto const event = app_manager::Event::from( { .on = true } );

    EXPECT_EQ( event.type, app_manager::Event::Type::led_request );
    EXPECT_TRUE( event.led_request.on );
}

} // namespace
