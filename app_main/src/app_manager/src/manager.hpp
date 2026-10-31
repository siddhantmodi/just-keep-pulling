/**
 * @file manager.hpp
 * @brief app_manager::Manager: the app manager's logic, with no thread of its own.
 *
 * The manager thread (app_manager.cpp) takes Events off its queue and calls
 * handle(); tests call it directly.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "channels.hpp"
#include "led_svc.hpp"

#include <cstdint>
#include <fw/error.hpp>
#include <type_traits>

namespace app_manager
{

/**
 * @brief One thing for the manager to handle: a tag and its payload. A plain
 *        tagged union rather than std::variant, whose std::visit costs code size.
 */
struct Event {
    enum class Type : std::uint8_t {
        led_request,
    };

    Type type;
    union {
        channels::LedRequest led_request;
    };

    [[nodiscard]] static Event from( const channels::LedRequest &request ) noexcept
    {
        Event event{};
        event.type = Type::led_request;
        event.led_request = request;
        return event;
    }
};

static_assert( std::is_trivially_copyable_v<Event>, "events are copied through a k_msgq" );

class Manager
{
public:
    constexpr explicit Manager( led_svc::ILed &led ) noexcept
        : led_{ led }
    {
    }

    /** @brief Publish the current state so readers see it before any request. */
    [[nodiscard]] fw::Result<> start();

    [[nodiscard]] fw::Result<> handle( const Event &event );

private:
    /** @brief Apply the request, then publish the LED's actual state (unchanged if the request failed). */
    fw::Result<> on_led_request( const channels::LedRequest &request );
    fw::Result<> publish_led_state();

    led_svc::ILed &led_;
};

} // namespace app_manager
