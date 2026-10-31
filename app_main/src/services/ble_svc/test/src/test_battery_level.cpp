/**
 * @file test_battery_level.cpp
 * @brief Unit tests of the battery level estimate.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "battery_level.hpp"

#include <gtest/gtest.h>

namespace
{

using ble_svc::battery_level_percent;

TEST( BatteryLevel, EmptyAtThePowerOffVoltageAndBelow )
{
    EXPECT_EQ( battery_level_percent( 3300 ), 0 );
    EXPECT_EQ( battery_level_percent( 0 ), 0 );
}

TEST( BatteryLevel, FullAtTheChargeVoltageAndAbove )
{
    EXPECT_EQ( battery_level_percent( 4200 ), 100 );
    EXPECT_EQ( battery_level_percent( 4350 ), 100 );
}

TEST( BatteryLevel, FollowsTheCurve )
{
    EXPECT_EQ( battery_level_percent( 3750 ), 25 );
    EXPECT_EQ( battery_level_percent( 3900 ), 65 );
    EXPECT_EQ( battery_level_percent( 3950 ), 72 ); // halfway from 65 to 80
}

TEST( BatteryLevel, NeverDecreasesWithVoltage )
{
    unsigned previous = 0;
    for( std::uint32_t mv = 3000; mv <= 4400; mv += 10 ) {
        unsigned const level = battery_level_percent( mv );
        EXPECT_GE( level, previous ) << mv << " mV";
        previous = level;
    }
}

static_assert( battery_level_percent( 4100 ) == 92, "usable at compile time" );

} // namespace
