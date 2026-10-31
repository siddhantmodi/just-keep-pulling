/**
 * @file measurement.hpp
 * @brief measurement_manager::Measurement: the measurement manager's logic, with no thread of its own.
 *
 * The manager thread (measurement_manager.cpp) takes Events off its queue and
 * calls handle(); while measuring, it calls sample() between events. Tests
 * call both directly.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "ble_svc.hpp"
#include "channels.hpp"
#include "load_cell_svc.hpp"
#include "progressor.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fw/error.hpp>
#include <hal/load_cell_adc.hpp>
#include <optional>
#include <string_view>
#include <type_traits>

namespace measurement_manager
{

/** @brief The version reported to the client: Crimpdeq's, which the Tindeq app accepts. */
inline constexpr std::string_view app_version = "2.0.4";

/** @brief The battery voltage reported until the PMIC's fuel gauge is read: a charged cell's, so the app shows no warning. */
inline constexpr std::uint32_t placeholder_battery_mv = 4000;

/** @brief Longest time a batch collects samples, so low rates still update the client often (Crimpdeq's). */
inline constexpr std::chrono::milliseconds max_batch_duration{ 50 };

/** @brief Consecutive failed reads after which the measurement stops, rather than retrying forever. */
inline constexpr unsigned max_read_failures = 5;

/**
 * @brief One thing for the manager to handle: a tag and its payload. A plain
 *        tagged union rather than std::variant, whose std::visit costs code size.
 */
struct Event {
    enum class Type : std::uint8_t {
        command,
        client_connection,
    };

    Type type;
    union {
        progressor::Command command;
        channels::ClientConnection client_connection;
    };

    [[nodiscard]] static Event from( const progressor::Command &command ) noexcept
    {
        Event event{};
        event.type = Type::command;
        event.command = command;
        return event;
    }

    [[nodiscard]] static Event from( const channels::ClientConnection &connection ) noexcept
    {
        Event event{};
        event.type = Type::client_connection;
        event.client_connection = connection;
        return event;
    }
};

static_assert( std::is_trivially_copyable_v<Event>, "events are copied through a k_msgq" );

class Measurement
{
public:
    constexpr Measurement( load_cell_svc::ILoadCell &load_cell, ble_svc::IClient &client, const progressor::ProgressorId &id ) noexcept
        : load_cell_{ load_cell }
        , client_{ client }
        , id_{ id }
    {
    }

    /** @brief Zero the scale, as Crimpdeq does at boot, so readings start near 0 kg before the client tares. */
    [[nodiscard]] fw::Result<> start();

    [[nodiscard]] fw::Result<> handle( const Event &event );

    [[nodiscard]] bool measuring() const noexcept
    {
        return rate_.has_value();
    }

    /**
     * @brief While measuring, wait for the next sample and add it to the batch,
     *        sending the batch once full. Fails with operation_not_permitted when
     *        not measuring, and with the load cell's error when a read fails.
     *
     * Timestamps count microseconds from one sample period before the first
     * sample, as if sampling had started at 0.
     */
    [[nodiscard]] fw::Result<> sample();

    /** @brief Batches dropped since boot because the link was too slow. */
    [[nodiscard]] std::uint32_t dropped_batches() const noexcept
    {
        return dropped_batches_;
    }

private:
    fw::Result<> on_command( const progressor::Command &command );
    fw::Result<> on_client_connection( const channels::ClientConnection &connection );
    fw::Result<> start_measurement();
    /** @brief Stop sampling; send the pending samples first if @p send_pending. */
    fw::Result<> stop_measurement( bool send_pending );
    /**
     * @brief Send the pending samples, if any. A packet the link has no room for is
     *        dropped and counted; nobody listening is not an error; anything else is logged.
     */
    void send_batch();
    fw::Result<> reply( const progressor::Packet &packet );
    /** @brief Samples per batch: what one notification carries, and no more than max_batch_duration of them. */
    [[nodiscard]] std::size_t batch_capacity() const;

    load_cell_svc::ILoadCell &load_cell_;
    ble_svc::IClient &client_;
    progressor::ProgressorId id_;
    std::optional<hal::SampleRate> rate_; // empty: not measuring
    std::optional<std::uint64_t> origin_us_;
    std::array<progressor::Sample, progressor::max_samples_per_packet> batch_{};
    std::size_t batch_size_{ 0 };
    std::size_t batch_capacity_{ 1 };
    unsigned read_failures_{ 0 };
    std::uint32_t dropped_batches_{ 0 };
};

} // namespace measurement_manager
