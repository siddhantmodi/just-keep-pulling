/**
 * @file test_queue_ref.cpp
 * @brief Unit tests for fw::QueueRef against a real k_msgq.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

#include <cstdint>
#include <fw/queue_ref.hpp>
#include <gtest/gtest.h>
#include <system_error>

namespace
{

struct Item {
    std::uint8_t id;
    std::int16_t value;
};

} // namespace

K_MSGQ_DEFINE( test_msgq, sizeof( Item ), 2, alignof( Item ) );

namespace
{

constexpr fw::QueueRef<Item> queue{ test_msgq };

class QueueRefTest : public ::testing::Test
{
protected:
    void TearDown() override
    {
        k_msgq_purge( &test_msgq );
    }
};

TEST_F( QueueRefTest, GetReturnsItemsInOrder )
{
    ASSERT_TRUE( queue.put( { .id = 1, .value = -5 } ) );
    ASSERT_TRUE( queue.put( { .id = 2, .value = 7 } ) );
    EXPECT_EQ( queue.size(), 2U );

    auto first = queue.get( K_NO_WAIT );
    auto second = queue.get( K_NO_WAIT );

    ASSERT_TRUE( first );
    ASSERT_TRUE( second );
    EXPECT_EQ( first->id, 1 );
    EXPECT_EQ( first->value, -5 );
    EXPECT_EQ( second->id, 2 );
    EXPECT_EQ( queue.size(), 0U );
}

TEST_F( QueueRefTest, PutFailsWithoutBlockingWhenFull )
{
    ASSERT_TRUE( queue.put( { .id = 1, .value = 0 } ) );
    ASSERT_TRUE( queue.put( { .id = 2, .value = 0 } ) );

    auto ret = queue.put( { .id = 3, .value = 0 } );

    ASSERT_FALSE( ret );
    EXPECT_EQ( ret.error(), std::errc::no_message );
}

TEST_F( QueueRefTest, GetFailsWhenEmpty )
{
    auto ret = queue.get( K_NO_WAIT );

    ASSERT_FALSE( ret );
    EXPECT_EQ( ret.error(), std::errc::no_message );
}

TEST_F( QueueRefTest, GetTimesOut )
{
    auto ret = queue.get( K_MSEC( 10 ) );

    ASSERT_FALSE( ret );
    EXPECT_EQ( ret.error(), std::errc::resource_unavailable_try_again );
}

} // namespace
