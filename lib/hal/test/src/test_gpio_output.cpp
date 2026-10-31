/**
 * @file test_gpio.cpp
 * @brief hal::zephyr::GpioOutput against native_sim's emulated GPIO (led0 on gpio_emul).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>

#include <gtest/gtest.h>
#include <hal/zephyr/gpio_output.hpp>
#include <system_error>

namespace
{

constexpr gpio_dt_spec led = GPIO_DT_SPEC_GET( DT_ALIAS( led0 ), gpios );

/** Physical pin level the emulator saw for a logical level, honouring GPIO_ACTIVE_LOW. */
int physical( bool active )
{
    bool const active_low = ( led.dt_flags & GPIO_ACTIVE_LOW ) != 0;
    return ( active != active_low ) ? 1 : 0;
}

TEST( GpioOutputTest, InitDrivesInitialLevel )
{
    hal::zephyr::GpioOutput output{ led };

    ASSERT_TRUE( output.init( false ) );
    EXPECT_EQ( gpio_emul_output_get( led.port, led.pin ), physical( false ) );

    ASSERT_TRUE( output.init( true ) );
    EXPECT_EQ( gpio_emul_output_get( led.port, led.pin ), physical( true ) );
}

TEST( GpioOutputTest, SetDrivesPin )
{
    hal::zephyr::GpioOutput output{ led };
    ASSERT_TRUE( output.init( false ) );

    ASSERT_TRUE( output.set( true ) );
    EXPECT_EQ( gpio_emul_output_get( led.port, led.pin ), physical( true ) );

    ASSERT_TRUE( output.set( false ) );
    EXPECT_EQ( gpio_emul_output_get( led.port, led.pin ), physical( false ) );
}

TEST( GpioOutputTest, InitFailsWithoutController )
{
    hal::zephyr::GpioOutput output{ gpio_dt_spec{ .port = nullptr, .pin = 0, .dt_flags = 0 } };

    auto ret = output.init( false );

    ASSERT_FALSE( ret );
    EXPECT_EQ( ret.error(), std::errc::no_such_device );
}

} // namespace
