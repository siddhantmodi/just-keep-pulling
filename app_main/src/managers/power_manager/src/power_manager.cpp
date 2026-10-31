/**
 * @file power_manager.cpp
 * @brief The power manager's plumbing: timers and a zbus listener feeding its
 *        event queue, and the thread that hands events to power_manager::Power
 *        (battery) and power_manager::Activity (inactivity).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "power_manager.hpp"

#include "activity.hpp"
#include "ble_svc.hpp"
#include "channels.hpp"
#include "event.hpp"
#include "motion_svc.hpp"
#include "pmic_svc.hpp"
#include "power.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <fw/queue_ref.hpp>

LOG_MODULE_DECLARE( power_manager, CONFIG_APP_POWER_MANAGER_LOG_LEVEL );

// NOLINTNEXTLINE(modernize-avoid-c-arrays): the Zephyr macro declares the queue's buffer.
K_MSGQ_DEFINE( power_manager_events_msgq, sizeof( power_manager::Event ), CONFIG_APP_POWER_MANAGER_QUEUE_DEPTH, alignof( power_manager::Event ) );

namespace
{

using power_manager::Event;

/** @brief The production logic, built on first use: its dependencies come from other files, so it cannot be constinit. */
power_manager::Power &power()
{
    static power_manager::Power instance{ pmic_svc::pmic(), ble_svc::client() };
    return instance;
}

power_manager::Activity &activity()
{
    static power_manager::Activity instance{ motion_svc::motion(),
                                             ble_svc::client(),
                                             { .disconnected = std::chrono::seconds{ CONFIG_APP_POWER_MANAGER_IDLE_TIMEOUT_S },
                                               .connected = std::chrono::seconds{ CONFIG_APP_POWER_MANAGER_CONNECTED_IDLE_TIMEOUT_S } } };
    return instance;
}

constinit fw::QueueRef<Event> events{ power_manager_events_msgq };
constinit std::atomic<std::uint32_t> dropped_events{ 0 };

/** @brief Queue @p event without waiting: from timers (interrupt context) and zbus listeners. */
void post( const Event &event )
{
    if( !events.put( event ) ) {
        dropped_events.fetch_add( 1, std::memory_order_relaxed );
    }
}

void on_battery_timer( k_timer * /*timer*/ )
{
    post( Event::of( Event::Type::battery_check ) );
}

void on_activity_timer( k_timer * /*timer*/ )
{
    post( Event::of( Event::Type::activity_check ) );
}

/**
 * @brief zbus listener: runs in the publisher's thread with the channel locked,
 *        so it only copies the message into the event queue and returns.
 */
void on_message( const zbus_channel *chan )
{
    if( channels::progressor_command.is( chan ) ) {
        post( Event::command_from( channels::progressor_command.const_msg().opcode ) );
    } else if( channels::client_connection.is( chan ) ) {
        post( Event::change( Event::Type::client_connection, channels::client_connection.const_msg().connected ) );
    } else if( channels::measurement_state.is( chan ) ) {
        post( Event::change( Event::Type::measurement_state, channels::measurement_state.const_msg().measuring ) );
    }
}

} // namespace

K_TIMER_DEFINE( power_manager_battery_timer, on_battery_timer, nullptr );
K_TIMER_DEFINE( power_manager_activity_timer, on_activity_timer, nullptr );

ZBUS_LISTENER_DEFINE( power_manager_listener, on_message );
ZBUS_CHAN_ADD_OBS( progressor_command_chan, power_manager_listener, 0 );
ZBUS_CHAN_ADD_OBS( client_connection_chan, power_manager_listener, 0 );
ZBUS_CHAN_ADD_OBS( measurement_state_chan, power_manager_listener, 0 );

namespace
{

void schedule_battery_check()
{
    k_timer_start( &power_manager_battery_timer, K_MSEC( power().next_check().count() ), K_NO_WAIT );
}

fw::Result<> handle( const Event &event )
{
    switch( event.type ) {
    case Event::Type::battery_check:
    {
        auto ret = power().check();
        schedule_battery_check();
        return ret;
    }
    case Event::Type::activity_check:
        return activity().check();
    case Event::Type::command:
        activity().on_command( event.opcode );
        return {};
    case Event::Type::client_connection:
        activity().on_client_connection( event.active );
        return {};
    case Event::Type::measurement_state:
        activity().on_measurement( event.active );
        return {};
    }
    return {};
}

void run( void * /*p1*/, void * /*p2*/, void * /*p3*/ )
{
    while( true ) {
        auto event = events.get( K_FOREVER );
        if( !event ) {
            continue;
        }
        if( auto ret = handle( *event ); !ret ) {
            LOG_ERR( "event %u failed: %d", static_cast<unsigned>( event->type ), fw::err::to_zephyr( ret.error() ) );
        }
        if( auto const dropped = dropped_events.exchange( 0, std::memory_order_relaxed ); dropped != 0 ) {
            LOG_WRN( "event queue full, %u events dropped", static_cast<unsigned>( dropped ) );
        }
    }
}

} // namespace

// Created suspended; start() runs it once main() has initialized the PMIC and the IMU.
// NOLINTNEXTLINE(modernize-avoid-c-arrays): the Zephyr macro declares the thread's stack.
K_THREAD_DEFINE( power_manager_thread, CONFIG_APP_POWER_MANAGER_STACK_SIZE, run, nullptr, nullptr, nullptr, CONFIG_APP_POWER_MANAGER_PRIORITY, 0,
                 SYS_FOREVER_MS );

namespace power_manager
{

fw::Result<> start()
{
    // Built here, before the thread runs. The first battery check also runs
    // here, so the battery channel holds a reading before anyone asks.
    static_cast<void>( activity() );
    auto first = power().check();
    schedule_battery_check();
    auto const interval = K_SECONDS( activity_check_interval.count() );
    k_timer_start( &power_manager_activity_timer, interval, interval );
    k_thread_start( power_manager_thread );
    return first;
}

} // namespace power_manager
