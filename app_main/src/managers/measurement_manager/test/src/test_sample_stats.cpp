/**
 * @file test_sample_stats.cpp
 * @brief measurement_manager::SampleStats.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sample_stats.hpp"

#include <gtest/gtest.h>

namespace
{

using load_cell_svc::ForceSample;
using measurement_manager::SampleStats;

TEST( SampleStatsTest, EmptyAndSingleSampleHaveNoSpreadOrRate )
{
    SampleStats stats;
    EXPECT_EQ( stats.count(), 0U );
    EXPECT_EQ( stats.sd_raw(), 0.0 );
    EXPECT_EQ( stats.samples_per_second(), 0.0 );
    EXPECT_EQ( stats.mean_kg(), 0.0 );

    stats.add( { .kg = 1.0F, .timestamp_us = 500, .raw = 42 } );
    EXPECT_EQ( stats.count(), 1U );
    EXPECT_EQ( stats.mean_raw(), 42.0 );
    EXPECT_EQ( stats.sd_raw(), 0.0 );
    EXPECT_EQ( stats.samples_per_second(), 0.0 );
    EXPECT_EQ( stats.min_raw(), 42 );
    EXPECT_EQ( stats.max_raw(), 42 );
}

TEST( SampleStatsTest, MeanSpreadAndRangeOfTheCodes )
{
    SampleStats stats;
    stats.add( { .kg = 1.0F, .timestamp_us = 0, .raw = -10 } );
    stats.add( { .kg = 2.0F, .timestamp_us = 1000, .raw = -12 } );
    stats.add( { .kg = 3.0F, .timestamp_us = 2000, .raw = -14 } );

    EXPECT_DOUBLE_EQ( stats.mean_raw(), -12.0 );
    EXPECT_DOUBLE_EQ( stats.sd_raw(), 2.0 );
    EXPECT_EQ( stats.min_raw(), -14 );
    EXPECT_EQ( stats.max_raw(), -10 );
    EXPECT_DOUBLE_EQ( stats.mean_kg(), 2.0 );
}

TEST( SampleStatsTest, RateAndLongestGap )
{
    SampleStats stats;
    stats.add( { .kg = 0.0F, .timestamp_us = 10'000, .raw = 5 } );
    stats.add( { .kg = 0.0F, .timestamp_us = 11'000, .raw = 5 } );
    stats.add( { .kg = 0.0F, .timestamp_us = 12'000, .raw = 6 } );
    stats.add( { .kg = 0.0F, .timestamp_us = 14'000, .raw = 6 } );
    stats.add( { .kg = 0.0F, .timestamp_us = 15'000, .raw = 5 } );

    EXPECT_EQ( stats.count(), 5U );
    EXPECT_EQ( stats.max_gap_us(), 2000U );
    EXPECT_DOUBLE_EQ( stats.samples_per_second(), 800.0 ); // 4 periods in 5 ms
}

} // namespace
