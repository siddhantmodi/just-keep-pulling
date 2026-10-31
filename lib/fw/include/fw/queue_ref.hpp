/**
 * @file queue_ref.hpp
 * @brief fw::QueueRef<T>: a k_msgq bound to its item type.
 *
 * k_msgq copies items as raw bytes of a size fixed when the queue is defined.
 * QueueRef ties the queue to its item type at compile time and checks (in
 * debug builds) that the sizes match. It stores only the queue's address:
 * @code
 * K_MSGQ_DEFINE( events_msgq, sizeof( Event ), 8, alignof( Event ) );
 * constinit fw::QueueRef<Event> events{ events_msgq };
 * @endcode
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/kernel.h>

#include <cstdint>
#include <expected>
#include <fw/error.hpp>
#include <type_traits>

namespace fw
{

/** @brief Types a k_msgq can carry: it copies items with memcpy. */
template<typename T>
concept QueueItem = std::is_trivially_copyable_v<T>;

template<QueueItem T>
class QueueRef
{
public:
    constexpr explicit QueueRef( k_msgq &queue ) noexcept
        : queue_{ &queue }
    {
    }

    /**
     * @brief Add @p item, waiting up to @p timeout for space. With K_NO_WAIT
     *        (the default) it never blocks, so it is safe in ISRs and zbus
     *        listeners. Fails with no_message if the queue is full.
     */
    [[nodiscard]] Result<> put( const T &item, k_timeout_t timeout = K_NO_WAIT ) const noexcept
    {
        __ASSERT( queue_->msg_size == sizeof( T ), "queue item size does not match T" );
        return err::check( k_msgq_put( queue_, &item, timeout ) );
    }

    /**
     * @brief Remove the oldest item, waiting up to @p timeout. Fails with no_message
     *        (K_NO_WAIT, queue empty) or resource_unavailable_try_again (timed out).
     */
    [[nodiscard]] Result<T> get( k_timeout_t timeout = K_FOREVER ) const noexcept
    {
        __ASSERT( queue_->msg_size == sizeof( T ), "queue item size does not match T" );
        T item{};
        if( auto ret = err::check( k_msgq_get( queue_, &item, timeout ) ); !ret ) {
            return std::unexpected( ret.error() );
        }
        return item;
    }

    [[nodiscard]] std::uint32_t size() const noexcept
    {
        return k_msgq_num_used_get( queue_ );
    }

private:
    k_msgq *queue_;
};

} // namespace fw
