/**
 * @file test_scoped_lock.cpp
 * @brief Unit tests for fw::ScopedLock against a real k_mutex.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

#include <fw/error.hpp>
#include <fw/scoped_lock.hpp>
#include <gtest/gtest.h>
#include <system_error>

K_MUTEX_DEFINE( test_mutex );

namespace
{

fw::Result<> fail_while_locked()
{
    fw::ScopedLock const lock{ test_mutex };
    if( test_mutex.lock_count == 1 ) {
        return std::unexpected( std::errc::io_error );
    }
    return {};
}

TEST( ScopedLockTest, HoldsMutexForScope )
{
    {
        fw::ScopedLock const lock{ test_mutex };
        EXPECT_EQ( test_mutex.lock_count, 1U );
        EXPECT_EQ( test_mutex.owner, k_current_get() );
    }
    EXPECT_EQ( test_mutex.lock_count, 0U );
}

TEST( ScopedLockTest, UnlocksOnEarlyReturn )
{
    EXPECT_FALSE( fail_while_locked() );
    EXPECT_EQ( test_mutex.lock_count, 0U );
}

} // namespace
