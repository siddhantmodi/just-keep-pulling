/**
 * @file event.hpp
 * @brief power_manager::Event: what the power manager's thread handles.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "progressor.hpp"

#include <cstdint>
#include <type_traits>

namespace power_manager
{

/**
 * @brief One thing for the manager to handle: a tag and its payload. A plain
 *        tagged union rather than std::variant, whose std::visit costs code size.
 */
struct Event {
    enum class Type : std::uint8_t {
        battery_check,     // battery timer
        activity_check,    // activity timer
        command,           // the client wrote a command (opcode)
        client_connection, // a client connected or disconnected (active)
        measurement_state, // a measurement started or stopped (active)
    };

    Type type;
    union {
        progressor::Opcode opcode;
        bool active;
    };

    [[nodiscard]] static constexpr Event of( Type type ) noexcept
    {
        Event event{};
        event.type = type;
        return event;
    }

    [[nodiscard]] static constexpr Event command_from( progressor::Opcode opcode ) noexcept
    {
        Event event{};
        event.type = Type::command;
        event.opcode = opcode;
        return event;
    }

    [[nodiscard]] static constexpr Event change( Type type, bool active ) noexcept
    {
        Event event{};
        event.type = type;
        event.active = active;
        return event;
    }
};

static_assert( std::is_trivially_copyable_v<Event>, "events are copied through a k_msgq" );

} // namespace power_manager
