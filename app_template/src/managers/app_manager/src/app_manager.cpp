/**
 * @file app_manager.cpp
 * @brief The app manager's plumbing: a zbus listener feeding its event queue, and
 *        the thread that hands queued events to app_manager::App.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "app_manager.hpp"

#include "app.hpp"
#include "channels.hpp"
#include "led_svc.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <atomic>
#include <cstdint>
#include <fw/queue_ref.hpp>

LOG_MODULE_REGISTER( app_manager, CONFIG_APP_MANAGER_LOG_LEVEL );

// NOLINTNEXTLINE(modernize-avoid-c-arrays): the Zephyr macro declares the queue's buffer.
K_MSGQ_DEFINE( app_manager_events_msgq, sizeof( app_manager::Event ), CONFIG_APP_MANAGER_QUEUE_DEPTH, alignof( app_manager::Event ) );

namespace
{

/** @brief The production manager, built on first use: its LED comes from another file, so it cannot be constinit. */
app_manager::App &app()
{
    static app_manager::App instance{ led_svc::led() };
    return instance;
}

constinit fw::QueueRef<app_manager::Event> events{ app_manager_events_msgq };
constinit std::atomic<std::uint32_t> dropped_events{ 0 };

/**
 * @brief zbus listener: runs in the publisher's thread with the channel locked,
 *        so it only copies the message into the event queue and returns.
 */
void on_message( const zbus_channel *chan )
{
    app_manager::Event event{};
    if( channels::led_request.is( chan ) ) {
        event = app_manager::Event::from( channels::led_request.const_msg() );
    } else {
        return;
    }

    if( !events.put( event ) ) {
        std::uint32_t const dropped = dropped_events.fetch_add( 1, std::memory_order_relaxed ) + 1;
        LOG_WRN( "event queue full, dropped event %u (%u dropped so far)", static_cast<unsigned>( event.type ), static_cast<unsigned>( dropped ) );
    }
}

void run( void * /*p1*/, void * /*p2*/, void * /*p3*/ )
{
    while( true ) {
        auto event = events.get( K_FOREVER );
        if( !event ) {
            continue;
        }
        if( auto ret = app().handle( *event ); !ret ) {
            LOG_ERR( "event %u failed: %d", static_cast<unsigned>( event->type ), fw::err::to_zephyr( ret.error() ) );
        }
    }
}

} // namespace

ZBUS_LISTENER_DEFINE( app_manager_listener, on_message );
ZBUS_CHAN_ADD_OBS( led_request_chan, app_manager_listener, 0 );

// Created suspended; start() runs it once main() has initialized the services.
// NOLINTNEXTLINE(modernize-avoid-c-arrays): the Zephyr macro declares the thread's stack.
K_THREAD_DEFINE( app_manager_thread, CONFIG_APP_MANAGER_STACK_SIZE, run, nullptr, nullptr, nullptr, CONFIG_APP_MANAGER_PRIORITY, 0, SYS_FOREVER_MS );

namespace app_manager
{

fw::Result<> start()
{
    // Builds the manager here, before its thread runs.
    if( auto ret = app().start(); !ret ) {
        return ret;
    }
    k_thread_start( app_manager_thread );
    return {};
}

} // namespace app_manager
