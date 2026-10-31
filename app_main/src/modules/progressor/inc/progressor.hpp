/**
 * @file progressor.hpp
 * @brief The Tindeq Progressor Bluetooth protocol: decoding commands and encoding responses.
 *
 * Pure functions, no Zephyr. Clients write commands to the control point
 * characteristic and receive responses as data point notifications. Both use
 * the same format: a 1-byte code, a 1-byte length and the value; the length
 * and value are only present when there is a payload. Multi-byte values are
 * little-endian.
 *
 * Covers Tindeq's public API (tare, start/stop measurement, shutdown, battery
 * voltage) plus the commands Crimpdeq answers because the Tindeq app uses
 * them (app version, Progressor ID) and Crimpdeq's add-calibration-point.
 * Anything else, e.g. the undocumented peak-RFD commands, decodes as
 * not_supported.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <fw/error.hpp>
#include <span>
#include <string_view>

namespace progressor
{

enum class Opcode : std::uint8_t {
    tare = 0x64,
    start_measurement = 0x65,
    stop_measurement = 0x66,
    add_calibration_point = 0x69, // Crimpdeq's; weight in kg as f32
    get_app_version = 0x6B,
    shutdown = 0x6E,
    sample_battery = 0x6F,
    get_progressor_id = 0x70,
};

enum class ResponseCode : std::uint8_t {
    command_response = 0x00, // battery voltage, app version, Progressor ID
    weight_measurement = 0x01,
    low_power_warning = 0x04,
};

struct Command {
    Opcode opcode;
    float calibration_kg; // add_calibration_point only
};

/** @brief One weight sample: kilograms, and microseconds since the measurement started. */
struct Sample {
    float kg;
    std::uint32_t timestamp_us;
};

/** @brief The device's 6-byte Progressor ID, most significant byte first (as displayed). */
using ProgressorId = std::array<std::uint8_t, 6>;

inline constexpr std::size_t header_size = 2;
inline constexpr std::size_t sample_size = 8;
/** @brief Samples in one notification: as many as the largest ATT MTU (247) carries. */
inline constexpr std::size_t max_samples_per_packet = 30;
inline constexpr std::size_t max_packet_size = header_size + ( max_samples_per_packet * sample_size );

/** @brief A response, ready to send as a data point notification. */
struct Packet {
    std::array<std::uint8_t, max_packet_size> data{};
    std::size_t size{ 0 };

    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept
    {
        return { data.data(), size };
    }
};

/**
 * @brief Samples that fit in one notification for a negotiated ATT MTU, between 1 and
 *        max_samples_per_packet. A notification costs 3 bytes of ATT header plus our 2-byte header.
 */
[[nodiscard]] constexpr std::size_t samples_per_packet( std::uint16_t att_mtu ) noexcept
{
    constexpr std::size_t overhead = 3 + header_size;
    std::size_t const fit = att_mtu > overhead ? ( att_mtu - overhead ) / sample_size : 0;
    if( fit == 0 ) {
        return 1;
    }
    return fit < max_samples_per_packet ? fit : max_samples_per_packet;
}

/**
 * @brief Decode a control point write.
 *
 * Commands without a payload are accepted with or without a length byte, and
 * trailing bytes are ignored, as Crimpdeq does. add_calibration_point takes a
 * finite, non-negative f32, either right after the opcode (as written by hand
 * in nRF Connect) or after a length byte of 4.
 *
 * Fails with not_supported for an unknown opcode and invalid_argument for an
 * empty write or a malformed payload.
 */
[[nodiscard]] fw::Result<Command> decode_command( std::span<const std::uint8_t> bytes ) noexcept;

[[nodiscard]] Packet encode_battery_voltage( std::uint32_t millivolts ) noexcept;

/** @brief The version as ASCII, truncated to the largest payload. */
[[nodiscard]] Packet encode_app_version( std::string_view version ) noexcept;

/** @brief The ID on the wire is least significant byte first. */
[[nodiscard]] Packet encode_progressor_id( const ProgressorId &id ) noexcept;

/** @brief Fails with invalid_argument for no samples or more than max_samples_per_packet. */
[[nodiscard]] fw::Result<Packet> encode_weight_measurements( std::span<const Sample> samples ) noexcept;

[[nodiscard]] Packet encode_low_power_warning() noexcept;

} // namespace progressor
