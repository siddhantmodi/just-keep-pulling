/**
 * @file measurement_manager.cpp
 * @brief The measurement manager's plumbing: zbus listeners feeding its event
 *        queue, and the thread that hands events to measurement_manager::Manager
 *        and, while measuring, samples between them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "measurement_manager.hpp"

#include "ble_svc.hpp"
#include "channels.hpp"
#include "identity_svc.hpp"
#include "load_cell_svc.hpp"
#include "manager.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <atomic>
#include <cstdint>
#include <fw/queue_ref.hpp>
#include <tuple>

LOG_MODULE_DECLARE( measurement_manager, CONFIG_APP_MEASUREMENT_MANAGER_LOG_LEVEL );

// NOLINTNEXTLINE(modernize-avoid-c-arrays): the Zephyr macro declares the queue's buffer.
K_MSGQ_DEFINE( measurement_manager_events_msgq, sizeof( measurement_manager::Event ), CONFIG_APP_MEASUREMENT_MANAGER_QUEUE_DEPTH,
               alignof( measurement_manager::Event ) );

namespace
{

/**
 * @brief The production manager, built on first use: its dependencies come
 *        from other files, so it cannot be constinit. Takes the device ID as
 *        it is at start(); a new ID applies from the next boot, as the name does.
 */
measurement_manager::Manager &manager()
{
    static measurement_manager::Manager instance{ load_cell_svc::load_cell(), ble_svc::client(), identity_svc::identity().id() };
    return instance;
}

constinit fw::QueueRef<measurement_manager::Event> events{ measurement_manager_events_msgq };
constinit std::atomic<std::uint32_t> dropped_events{ 0 };

/**
 * @brief zbus listener: runs in the publisher's thread with the channel locked,
 *        so it only copies the message into the event queue and returns.
 */
void on_message( const zbus_channel *chan )
{
    measurement_manager::Event event{};
    if( channels::progressor_command.is( chan ) ) {
        event = measurement_manager::Event::from( channels::progressor_command.const_msg() );
    } else if( channels::client_connection.is( chan ) ) {
        event = measurement_manager::Event::from( channels::client_connection.const_msg() );
    } else {
        return;
    }

    if( !events.put( event ) ) {
        std::uint32_t const dropped = dropped_events.fetch_add( 1, std::memory_order_relaxed ) + 1;
        LOG_WRN( "event queue full, dropped event %u (%u dropped so far)", static_cast<unsigned>( event.type ), static_cast<unsigned>( dropped ) );
    }
}

/**
 * @brief Events first, in order; while measuring, one sample between them.
 *        sample() waits for the ADC, so a command waits at most one sample period.
 */
void run( void * /*p1*/, void * /*p2*/, void * /*p3*/ )
{
    while( true ) {
        auto event = events.get( manager().measuring() ? K_NO_WAIT : K_FOREVER );
        if( event ) {
            if( auto ret = manager().handle( *event ); !ret ) {
                LOG_ERR( "event %u failed: %d", static_cast<unsigned>( event->type ), fw::err::to_zephyr( ret.error() ) );
            }
            continue;
        }
        if( manager().measuring() ) {
            // Failures are logged by the manager, which stops measuring if they persist.
            std::ignore = manager().sample();
        }
    }
}

} // namespace

ZBUS_LISTENER_DEFINE( measurement_manager_listener, on_message );
ZBUS_CHAN_ADD_OBS( progressor_command_chan, measurement_manager_listener, 0 );
ZBUS_CHAN_ADD_OBS( client_connection_chan, measurement_manager_listener, 0 );

// Created suspended; start() runs it once main() has initialized the services.
// NOLINTNEXTLINE(modernize-avoid-c-arrays): the Zephyr macro declares the thread's stack.
K_THREAD_DEFINE( measurement_manager_thread, CONFIG_APP_MEASUREMENT_MANAGER_STACK_SIZE, run, nullptr, nullptr, nullptr,
                 CONFIG_APP_MEASUREMENT_MANAGER_PRIORITY, 0, SYS_FOREVER_MS );

namespace measurement_manager
{

fw::Result<> start()
{
    // Builds the manager here, before its thread runs.
    if( auto ret = manager().start(); !ret ) {
        return ret;
    }
    k_thread_start( measurement_manager_thread );
    return {};
}

} // namespace measurement_manager
