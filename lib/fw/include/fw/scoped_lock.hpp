/**
 * @file scoped_lock.hpp
 * @brief fw::ScopedLock: holds a k_mutex for the lifetime of a scope.
 *
 * Unlocks on every return path, including the early returns of fw::Result code.
 * Waits forever for the mutex, so it must not be used from an ISR.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/kernel.h>

namespace fw
{

class ScopedLock
{
public:
    explicit ScopedLock( k_mutex &mutex ) noexcept
        : mutex_{ mutex }
    {
        (void)k_mutex_lock( &mutex_, K_FOREVER ); // Cannot fail with K_FOREVER outside an ISR.
    }

    ~ScopedLock()
    {
        (void)k_mutex_unlock( &mutex_ );
    }

    ScopedLock( const ScopedLock & ) = delete;
    ScopedLock &operator=( const ScopedLock & ) = delete;
    ScopedLock( ScopedLock && ) = delete;
    ScopedLock &operator=( ScopedLock && ) = delete;

private:
    k_mutex &mutex_;
};

} // namespace fw
