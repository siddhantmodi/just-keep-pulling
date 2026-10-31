/**
 * @file test_indicator.cpp
 * @brief Unit tests of power_manager::Indicator against a fake LED that remembers its color.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "indicator.hpp"
#include "mock_led.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vector>

namespace
{

using led_svc::Color;
using power_manager::blink_steps;
using testing::_;
using testing::NiceMock;

class IndicatorTest : public testing::Test
{
protected:
    IndicatorTest()
    {
        ON_CALL( led_, color() ).WillByDefault( [this] { return color_; } );
        ON_CALL( led_, show( _ ) )
            .WillByDefault(
                [this]( Color color )
                {
                    color_ = color;
                    shown_.push_back( color );
                    return fw::Result<>{};
                } );
    }

    /** @brief The colors over @p steps blink steps. */
    std::vector<Color> blink( unsigned steps )
    {
        std::vector<Color> colors;
        for( unsigned i = 0; i < steps; ++i ) {
            EXPECT_TRUE( indicator_.on_blink_step() );
            colors.push_back( color_ );
        }
        return colors;
    }

    NiceMock<led_svc::MockLed> led_;
    power_manager::Indicator indicator_{ led_ };
    Color color_{ Color::off };
    std::vector<Color> shown_;
};

TEST_F( IndicatorTest, BlueWithoutAClient )
{
    ASSERT_TRUE( indicator_.start() );
    EXPECT_EQ( color_, Color::blue );
    EXPECT_FALSE( indicator_.blinking() );
}

TEST_F( IndicatorTest, GreenWithAClient )
{
    ASSERT_TRUE( indicator_.on_client_connection( true ) );
    EXPECT_EQ( color_, Color::green );

    ASSERT_TRUE( indicator_.on_client_connection( false ) );
    EXPECT_EQ( color_, Color::blue );
}

TEST_F( IndicatorTest, BlinksRedOnALowBattery )
{
    ASSERT_TRUE( indicator_.on_client_connection( true ) );
    ASSERT_TRUE( indicator_.on_battery( 3500 ) );

    EXPECT_TRUE( indicator_.blinking() );
    EXPECT_EQ( color_, Color::red ); // starts on
    EXPECT_EQ( blink( blink_steps ), ( std::vector{ Color::off, Color::off, Color::off, Color::red } ) );
}

TEST_F( IndicatorTest, StopsBlinkingWhenTheBatteryRecovers )
{
    ASSERT_TRUE( indicator_.on_battery( 3400 ) );
    ASSERT_TRUE( indicator_.on_battery( 3600 ) );

    EXPECT_FALSE( indicator_.blinking() );
    EXPECT_EQ( color_, Color::blue );
}

TEST_F( IndicatorTest, OffWhileSleeping )
{
    ASSERT_TRUE( indicator_.on_battery( 3400 ) );
    ASSERT_TRUE( indicator_.on_sleep( true ) );

    EXPECT_EQ( color_, Color::off );
    EXPECT_FALSE( indicator_.blinking() );

    ASSERT_TRUE( indicator_.on_sleep( false ) );
    EXPECT_EQ( color_, Color::red );
}

TEST_F( IndicatorTest, OnlyWritesTheLedWhenTheColorChanges )
{
    ASSERT_TRUE( indicator_.start() );
    ASSERT_TRUE( indicator_.on_battery( 3900 ) );
    ASSERT_TRUE( indicator_.on_battery( 3800 ) );

    EXPECT_EQ( shown_, ( std::vector{ Color::blue } ) );
}

} // namespace
