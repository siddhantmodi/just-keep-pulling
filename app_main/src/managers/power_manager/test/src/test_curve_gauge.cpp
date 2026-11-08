/**
 * @file test_curve_gauge.cpp
 * @brief Unit tests of power_manager::CurveGauge, the voltage-only stand-in for the fuel gauge.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "curve_gauge.hpp"

#include <chrono>
#include <gtest/gtest.h>

namespace
{

using power_manager::curve_percent;

TEST( CurvePercent, EmptyAtThePowerOffVoltageAndBelow )
{
    EXPECT_EQ( curve_percent( 3300 ), 0 );
    EXPECT_EQ( curve_percent( 0 ), 0 );
}

TEST( CurvePercent, FullAtTheChargeVoltageAndAbove )
{
    EXPECT_EQ( curve_percent( 4200 ), 100 );
    EXPECT_EQ( curve_percent( 4350 ), 100 );
}

TEST( CurvePercent, FollowsTheCurve )
{
    EXPECT_EQ( curve_percent( 3750 ), 25 );
    EXPECT_EQ( curve_percent( 3900 ), 65 );
    EXPECT_EQ( curve_percent( 3950 ), 72 ); // halfway from 65 to 80
}

TEST( CurvePercent, NeverDecreasesWithVoltage )
{
    unsigned previous = 0;
    for( std::uint32_t mv = 3000; mv <= 4400; mv += 10 ) {
        unsigned const level = curve_percent( mv );
        EXPECT_GE( level, previous ) << mv << " mV";
        previous = level;
    }
}

static_assert( curve_percent( 4100 ) == 92, "usable at compile time" );

TEST( CurveGauge, ReportsTheCurveAndKeepsNoState )
{
    power_manager::CurveGauge gauge;
    EXPECT_TRUE( gauge.init( { .millivolts = 3750 }, {} ) );
    EXPECT_EQ( gauge.update( { .millivolts = 3750 }, std::chrono::seconds{ 5 } ), 25.0F );
    EXPECT_EQ( gauge.state_size(), 0U );
}

} // namespace
