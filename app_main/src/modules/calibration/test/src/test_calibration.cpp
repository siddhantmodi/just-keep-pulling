/**
 * @file test_calibration.cpp
 * @brief Unit tests for the load-cell calibration maths.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "calibration.hpp"

#include <array>
#include <gtest/gtest.h>
#include <limits>
#include <system_error>

using calibration::Point;

namespace
{

// 0.109 g per code: 1 kg = 1000 / 0.109 = 9174.3 codes.
constexpr double codes_per_kg = 1000.0 / 0.109;

TEST( ToKgTest, AppliesTareAndFactor )
{
    EXPECT_FLOAT_EQ( calibration::to_kg( 10'000, 1'000, 0.109F ), 0.981F ); // 9000 codes
    EXPECT_FLOAT_EQ( calibration::to_kg( 1'000, 1'000, 0.109F ), 0.0F );
    EXPECT_FLOAT_EQ( calibration::to_kg( -8'000, 1'000, 0.109F ), -0.981F ); // a push reads negative
}

TEST( ToKgTest, DoesNotOverflowAtCodeExtremes )
{
    constexpr std::int32_t max = ( 1 << 23 ) - 1;
    constexpr std::int32_t min = -( 1 << 23 );

    EXPECT_GT( calibration::to_kg( max, min, 0.109F ), 0.0F );
    EXPECT_LT( calibration::to_kg( min, max, 0.109F ), 0.0F );
}

TEST( IsPlausibleTest, AcceptsTenfoldEitherWayAndEitherSign )
{
    EXPECT_TRUE( calibration::is_plausible( 0.109F ) );
    EXPECT_TRUE( calibration::is_plausible( 1.0F ) );
    EXPECT_TRUE( calibration::is_plausible( 0.0115F ) );
    EXPECT_TRUE( calibration::is_plausible( -0.109F ) );
    EXPECT_FALSE( calibration::is_plausible( 1.2F ) );
    EXPECT_FALSE( calibration::is_plausible( 0.01F ) );
    EXPECT_FALSE( calibration::is_plausible( 0.0F ) );
}

TEST( FitFactorTest, TwoPointsGiveTheirSlope )
{
    std::array const points{ Point{ .raw = 1000.0, .kg = 0.0F }, Point{ .raw = 1000.0 + ( 20.0 * codes_per_kg ), .kg = 20.0F } };

    auto factor = calibration::fit_factor( points );

    ASSERT_TRUE( factor );
    EXPECT_NEAR( *factor, 0.109F, 1e-6F );
}

TEST( FitFactorTest, LineGoesThroughTheZeroLoadPoints )
{
    // Two noisy no-load points average to raw 1000; the line must pass through it.
    std::array const points{ Point{ .raw = 990.0, .kg = 0.0F }, Point{ .raw = 1010.0, .kg = 0.0F },
                             Point{ .raw = 1000.0 + ( 10.0 * codes_per_kg ), .kg = 10.0F } };

    auto factor = calibration::fit_factor( points );

    ASSERT_TRUE( factor );
    EXPECT_NEAR( *factor, 0.109F, 1e-6F );
}

TEST( FitFactorTest, WithoutZeroLoadPointsLineGoesThroughTheMean )
{
    std::array const points{ Point{ .raw = 5.0 * codes_per_kg, .kg = 5.0F }, Point{ .raw = 15.0 * codes_per_kg, .kg = 15.0F },
                             Point{ .raw = 25.0 * codes_per_kg, .kg = 25.0F } };

    auto factor = calibration::fit_factor( points );

    ASSERT_TRUE( factor );
    EXPECT_NEAR( *factor, 0.109F, 1e-6F );
}

TEST( FitFactorTest, LeastSquaresAveragesNoise )
{
    // +/-50 codes of noise on the loaded points cancels out.
    std::array const points{ Point{ .raw = 0.0, .kg = 0.0F }, Point{ .raw = ( 10.0 * codes_per_kg ) + 50.0, .kg = 10.0F },
                             Point{ .raw = ( 10.0 * codes_per_kg ) - 50.0, .kg = 10.0F } };

    auto factor = calibration::fit_factor( points );

    ASSERT_TRUE( factor );
    EXPECT_NEAR( *factor, 0.109F, 1e-6F );
}

TEST( FitFactorTest, AcceptsReversedWiring )
{
    std::array const points{ Point{ .raw = 0.0, .kg = 0.0F }, Point{ .raw = -20.0 * codes_per_kg, .kg = 20.0F } };

    auto factor = calibration::fit_factor( points );

    ASSERT_TRUE( factor );
    EXPECT_NEAR( *factor, -0.109F, 1e-6F );
}

TEST( FitFactorTest, RejectsFewerThanTwoPoints )
{
    std::array const one{ Point{ .raw = 1000.0, .kg = 10.0F } };

    EXPECT_EQ( calibration::fit_factor( one ).error(), std::errc::invalid_argument );
    EXPECT_EQ( calibration::fit_factor( {} ).error(), std::errc::invalid_argument );
}

TEST( FitFactorTest, RejectsPointsWithTheSameReading )
{
    // The load changed but the reading did not: a wiring fault.
    std::array const points{ Point{ .raw = 1000.0, .kg = 0.0F }, Point{ .raw = 1000.0, .kg = 20.0F } };

    EXPECT_EQ( calibration::fit_factor( points ).error(), std::errc::invalid_argument );
}

TEST( FitFactorTest, RejectsImplausibleFactor )
{
    // 2 g per code, ~18x the default: e.g. the wrong weight was entered.
    std::array const points{ Point{ .raw = 0.0, .kg = 0.0F }, Point{ .raw = 10'000.0, .kg = 20.0F } };

    EXPECT_EQ( calibration::fit_factor( points ).error(), std::errc::result_out_of_range );
}

} // namespace
