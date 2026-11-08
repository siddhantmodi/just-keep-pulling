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
#include "sample_stats.hpp"

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

/** @brief The version reported to the client (Kconfig; Crimpdeq's by default). */
inline constexpr std::string_view app_version = CONFIG_APP_MEASUREMENT_MANAGER_APP_VERSION;

/** @brief Longest time a batch collects samples, so low rates still update the client often (Crimpdeq's). */
inline constexpr std::chrono::milliseconds max_batch_duration{ 50 };

/** @brief A load at least this far from zero, either way, is someone pulling: well above noise and drift after a tare. */
inline constexpr float pull_threshold_kg = 2.0F;
/** @brief A pull is reported at most this often: the power manager only checks for activity every 10 s. */
inline constexpr std::chrono::seconds pull_report_interval{ 10 };
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
        config_command,
    };

    Type type;
    union {
        progressor::Command command;
        channels::ClientConnection client_connection;
        channels::ConfigCommand config_command;
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

    [[nodiscard]] static Event from( const channels::ConfigCommand &command ) noexcept
    {
        Event event{};
        event.type = Type::config_command;
        event.config_command = command;
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

    /**
     * @brief Zero the scale, as Crimpdeq does at boot, so readings start near 0 kg
     *        before the client tares, and publish the calibration state.
     */
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
    /** @brief Calibrate or set the sample rate, publishing the calibration state as it goes. */
    fw::Result<> on_config_command( const channels::ConfigCommand &command );
    fw::Result<> add_calibration_point( float kg );
    void publish_calibration( channels::CalibrationStatus status ) const;
    fw::Result<> start_measurement();
    /** @brief Stop sampling; send the pending samples first if @p send_pending. */
    fw::Result<> stop_measurement( bool send_pending );
    /**
     * @brief Send the pending samples, if any. A packet the link has no room for is
     *        dropped and counted; nobody listening is not an error; anything else is logged.
     */
    void send_batch();
    fw::Result<> reply( const progressor::Packet &packet );
    /** @brief Log the measurement's SampleStats, its lost samples and the batches dropped. */
    void log_stats() const;
    /** @brief Publish channels::measurement_state: a measurement starting or stopping is activity for the power manager. */
    void publish_state() const;
    /** @brief Publish channels::pull for a sample that shows one, at most every pull_report_interval. */
    void report_pull( const load_cell_svc::ForceSample &sample );
    /** @brief Samples per batch: what one notification carries, and no more than max_batch_duration of them. */
    [[nodiscard]] std::size_t batch_capacity() const;

    load_cell_svc::ILoadCell &load_cell_;
    ble_svc::IClient &client_;
    progressor::ProgressorId id_;
    std::optional<hal::SampleRate> rate_; // empty: not measuring
    std::optional<std::uint64_t> origin_us_;
    std::optional<std::uint64_t> last_pull_us_;
    std::array<progressor::Sample, progressor::max_samples_per_packet> batch_{};
    std::size_t batch_size_{ 0 };
    std::size_t batch_capacity_{ 1 };
    unsigned read_failures_{ 0 };
    std::uint32_t dropped_batches_{ 0 };
    SampleStats stats_{}; // logged when the measurement stops
};

} // namespace measurement_manager
