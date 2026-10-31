/**
 * @file test_channel_ref.cpp
 * @brief Unit tests for fw::ChannelRef against real zbus channels.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/zbus/zbus.h>

#include <cstdint>
#include <fw/channel_ref.hpp>
#include <gtest/gtest.h>
#include <memory>
#include <system_error>

namespace
{

struct Sample {
    std::int32_t value;
    bool flag;
};

struct NotAMessage {
    std::unique_ptr<int> owned; // Owning member: not memcpy-safe.
};

static_assert( fw::ZbusMessage<Sample> );
static_assert( !fw::ZbusMessage<NotAMessage> );

} // namespace

ZBUS_CHAN_DEFINE( sample_chan, Sample, nullptr, nullptr, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT( 0 ) );
ZBUS_CHAN_DEFINE( listened_chan, Sample, nullptr, nullptr, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT( 0 ) );
// Never published, so it always holds its initial message.
ZBUS_CHAN_DEFINE( untouched_chan, Sample, nullptr, nullptr, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(.value = 7, .flag = true ) );

namespace
{

constexpr fw::ChannelRef<Sample> sample{ sample_chan };
constexpr fw::ChannelRef<Sample> untouched{ untouched_chan };
constexpr fw::ChannelRef<Sample> listened{ listened_chan };

Sample seen_by_listener{};

void on_listened( const zbus_channel *chan )
{
    if( listened.is( chan ) ) {
        seen_by_listener = listened.const_msg();
    }
}

} // namespace

ZBUS_LISTENER_DEFINE( test_listener, on_listened );
ZBUS_CHAN_ADD_OBS( listened_chan, test_listener, 0 );

namespace
{

TEST( ChannelRefTest, ReadReturnsInitialMessage )
{
    auto msg = untouched.read();

    ASSERT_TRUE( msg );
    EXPECT_EQ( msg->value, 7 );
    EXPECT_TRUE( msg->flag );
}

TEST( ChannelRefTest, PublishThenReadRoundTrips )
{
    ASSERT_TRUE( sample.publish( { .value = -42, .flag = true } ) );

    auto msg = sample.read();

    ASSERT_TRUE( msg );
    EXPECT_EQ( msg->value, -42 );
    EXPECT_TRUE( msg->flag );
}

TEST( ChannelRefTest, IsMatchesOnlyItsOwnChannel )
{
    EXPECT_TRUE( sample.is( &sample_chan ) );
    EXPECT_FALSE( sample.is( &untouched_chan ) );
    EXPECT_EQ( &sample.native(), &sample_chan );
}

} // namespace

namespace
{

TEST( ChannelRefTest, ListenerSeesPublishedMessageViaConstMsg )
{
    ASSERT_TRUE( listened.publish( { .value = 99, .flag = true } ) );

    EXPECT_EQ( seen_by_listener.value, 99 );
    EXPECT_TRUE( seen_by_listener.flag );
}

} // namespace
