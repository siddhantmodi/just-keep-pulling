/**
 * @file test_led.cpp
 * @brief led_svc::Led on Zephyr's GPIO LED driver, against native_sim's emulated GPIOs.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "led.hpp"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/led.h>

#include <gtest/gtest.h>

namespace
{

using led_svc::Color;

/** @brief Whether the LED on @p pin is lit: they are active low. */
bool lit( gpio_pin_t pin )
{
    return gpio_emul_output_get( DEVICE_DT_GET( DT_NODELABEL( gpio0 ) ), pin ) == 0;
}

class LedTest : public testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE( led_.init() );
    }

    static void expect_lit( bool red, bool green, bool blue )
    {
        EXPECT_EQ( lit( 0 ), red );
        EXPECT_EQ( lit( 1 ), green );
        EXPECT_EQ( lit( 2 ), blue );
    }

    led_svc::Led led_{ LED_DT_SPEC_GET( DT_ALIAS( status_led_red ) ), LED_DT_SPEC_GET( DT_ALIAS( status_led_green ) ),
                       LED_DT_SPEC_GET( DT_ALIAS( status_led_blue ) ) };
};

TEST_F( LedTest, InitTurnsAllOff )
{
    ASSERT_TRUE( led_.show( Color::red ) );
    ASSERT_TRUE( led_.init() );
    expect_lit( false, false, false );
    EXPECT_EQ( led_.color(), Color::off );
}

TEST_F( LedTest, ShowsOneColorAtATime )
{
    ASSERT_TRUE( led_.show( Color::green ) );
    expect_lit( false, true, false );
    EXPECT_EQ( led_.color(), Color::green );

    ASSERT_TRUE( led_.show( Color::blue ) );
    expect_lit( false, false, true );

    ASSERT_TRUE( led_.show( Color::red ) );
    expect_lit( true, false, false );
}

TEST_F( LedTest, OffTurnsAllOff )
{
    ASSERT_TRUE( led_.show( Color::blue ) );
    ASSERT_TRUE( led_.show( Color::off ) );
    expect_lit( false, false, false );
    EXPECT_EQ( led_.color(), Color::off );
}

} // namespace
