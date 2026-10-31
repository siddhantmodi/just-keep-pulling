/**
 * @file power_manager.cpp
 * @brief The power manager's plumbing: a timer feeding its event queue, and
 *        the thread that hands events to power_manager::Power and re-arms the
 *        timer for the next battery check.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "power_manager.hpp"

#include "ble_svc.hpp"
#include "pmic_svc.hpp"
#include "power.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <fw/queue_ref.hpp>

LOG_MODULE_DECLARE( power_manager, CONFIG_APP_POWER_MANAGER_LOG_LEVEL );

// NOLINTNEXTLINE(modernize-avoid-c-arrays): the Zephyr macro declares the queue's buffer.
K_MSGQ_DEFINE( power_manager_events_msgq, sizeof( power_manager::Event ), CONFIG_APP_POWER_MANAGER_QUEUE_DEPTH, alignof( power_manager::Event ) );

namespace
{

/** @brief The production manager, built on first use: its dependencies come from other files, so it cannot be constinit. */
power_manager::Power &power()
{
    static power_manager::Power instance{ pmic_svc::pmic(), ble_svc::client() };
    return instance;
}

constinit fw::QueueRef<power_manager::Event> events{ power_manager_events_msgq };
constinit std::atomic<std::uint32_t> dropped_events{ 0 };

/** @brief Timer expiry, in interrupt context: queue a battery check without waiting. */
void on_timer( k_timer * /*timer*/ )
{
    if( !events.put( { .type = power_manager::Event::Type::battery_check } ) ) {
        dropped_events.fetch_add( 1, std::memory_order_relaxed );
    }
}

} // namespace

K_TIMER_DEFINE( power_manager_timer, on_timer, nullptr );

namespace
{

void schedule_next_check()
{
    auto const delay = power().next_check();
    k_timer_start( &power_manager_timer, K_MSEC( delay.count() ), K_NO_WAIT );
}

void run( void * /*p1*/, void * /*p2*/, void * /*p3*/ )
{
    while( true ) {
        auto event = events.get( K_FOREVER );
        if( !event ) {
            continue;
        }
        if( auto ret = power().handle( *event ); !ret ) {
            LOG_ERR( "event %u failed: %d", static_cast<unsigned>( event->type ), fw::err::to_zephyr( ret.error() ) );
        }
        if( auto const dropped = dropped_events.exchange( 0, std::memory_order_relaxed ); dropped != 0 ) {
            LOG_WRN( "event queue full, %u events dropped", static_cast<unsigned>( dropped ) );
        }
        schedule_next_check();
    }
}

} // namespace

// Created suspended; start() runs it once main() has initialized the PMIC.
// NOLINTNEXTLINE(modernize-avoid-c-arrays): the Zephyr macro declares the thread's stack.
K_THREAD_DEFINE( power_manager_thread, CONFIG_APP_POWER_MANAGER_STACK_SIZE, run, nullptr, nullptr, nullptr, CONFIG_APP_POWER_MANAGER_PRIORITY, 0,
                 SYS_FOREVER_MS );

namespace power_manager
{

fw::Result<> start()
{
    // The first check runs here, so the battery channel holds a reading before anyone asks.
    auto first = power().handle( { .type = Event::Type::battery_check } );
    schedule_next_check();
    k_thread_start( power_manager_thread );
    return first;
}

} // namespace power_manager
