/**
 * @file channel_ref.hpp
 * @brief fw::ChannelRef<T>: a zbus channel bound to its message type.
 *
 * zbus passes messages as void * plus a size checked at runtime. ChannelRef ties
 * the channel to its message type at compile time, so publishing or reading
 * the wrong struct does not compile. It stores only the channel's address, so
 * handles can be `inline constexpr` next to the channel definition:
 * @code
 * ZBUS_CHAN_DEFINE( led_request_chan, LedRequest, nullptr, nullptr, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT( 0 ) );
 * inline constexpr fw::ChannelRef<LedRequest> led_request{ led_request_chan };
 * @endcode
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#include <expected>
#include <fw/error.hpp>
#include <type_traits>

namespace fw
{

/**
 * @brief Types zbus can carry: it copies messages with memcpy, so they must be
 *        trivially copyable and standard-layout (plain structs, no ownership).
 */
template<typename T>
concept ZbusMessage = std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>;

template<ZbusMessage T>
class ChannelRef
{
public:
    constexpr explicit ChannelRef( const zbus_channel &chan ) noexcept
        : chan_{ &chan }
    {
    }

    /** @brief Publish @p msg, notifying observers. Fails with e.g. resource_unavailable_try_again on lock timeout. */
    [[nodiscard]] Result<> publish( const T &msg, k_timeout_t timeout = K_NO_WAIT ) const noexcept
    {
        __ASSERT( zbus_chan_msg_size( chan_ ) == sizeof( T ), "channel message size does not match T" );
        return err::check( zbus_chan_pub( chan_, &msg, timeout ) );
    }

    /** @brief Copy the channel's current message. Thread-safe; must not be called from a listener on this channel. */
    [[nodiscard]] Result<T> read( k_timeout_t timeout = K_NO_WAIT ) const noexcept
    {
        __ASSERT( zbus_chan_msg_size( chan_ ) == sizeof( T ), "channel message size does not match T" );
        T msg{};
        if( auto ret = err::check( zbus_chan_read( chan_, &msg, timeout ) ); !ret ) {
            return std::unexpected( ret.error() );
        }
        return msg;
    }

    /**
     * @brief The channel's current message, without locking. Only for a
     *        listener on this channel: it runs while the publisher holds the
     *        lock, so read() would deadlock there.
     */
    [[nodiscard]] const T &const_msg() const noexcept
    {
        __ASSERT( zbus_chan_msg_size( chan_ ) == sizeof( T ), "channel message size does not match T" );
        return *static_cast<const T *>( zbus_chan_const_msg( chan_ ) );
    }

    /** @brief True if @p chan is this channel, e.g. to dispatch on the channel a listener was notified for. */
    [[nodiscard]] constexpr bool is( const zbus_channel *chan ) const noexcept
    {
        return chan == chan_;
    }

    [[nodiscard]] constexpr const zbus_channel &native() const noexcept
    {
        return *chan_;
    }

private:
    const zbus_channel *chan_;
};

} // namespace fw
