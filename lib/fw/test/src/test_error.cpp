/**
 * @file test_error.cpp
 * @brief Unit tests for the fw::Result error model and Zephyr code conversions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cerrno>
#include <fw/error.hpp>
#include <gtest/gtest.h>
#include <system_error>

namespace
{

TEST( ErrorTest, CheckTreatsZeroAndPositiveAsSuccess )
{
    EXPECT_TRUE( fw::err::check( 0 ) );
    EXPECT_TRUE( fw::err::check( 5 ) );
}

TEST( ErrorTest, CheckMapsNegativeErrnoToErrc )
{
    auto ret = fw::err::check( -EIO );

    ASSERT_FALSE( ret );
    EXPECT_EQ( ret.error(), std::errc::io_error );
}

TEST( ErrorTest, ConversionsRoundTrip )
{
    EXPECT_EQ( fw::err::from_zephyr( -ENODEV ), std::errc::no_such_device );
    EXPECT_EQ( fw::err::to_zephyr( std::errc::no_such_device ), -ENODEV );
    EXPECT_EQ( fw::err::to_zephyr( fw::err::from_zephyr( -EAGAIN ) ), -EAGAIN );
}

} // namespace
