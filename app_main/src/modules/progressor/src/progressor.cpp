/**
 * @file progressor.cpp
 * @brief Tindeq Progressor protocol encoding and decoding.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "progressor.hpp"

#include <bit>
#include <expected>
#include <limits>
#include <ranges>
#include <system_error>

static_assert( std::numeric_limits<float>::is_iec559, "the protocol sends IEEE 754 floats" );

namespace progressor
{

namespace
{

constexpr std::size_t max_payload_size = max_packet_size - header_size;

/** @brief Builds a packet: code, length, then payload appended little-endian. */
class PacketWriter
{
public:
    explicit PacketWriter( ResponseCode code ) noexcept
    {
        packet_.data[0] = static_cast<std::uint8_t>( code );
        packet_.size = header_size;
    }

    void put_u8( std::uint8_t value ) noexcept
    {
        packet_.data[packet_.size++] = value;
    }

    void put_u32( std::uint32_t value ) noexcept
    {
        for( unsigned shift = 0; shift < 32; shift += 8 ) {
            put_u8( static_cast<std::uint8_t>( value >> shift ) );
        }
    }

    void put_f32( float value ) noexcept
    {
        put_u32( std::bit_cast<std::uint32_t>( value ) );
    }

    [[nodiscard]] Packet finish() noexcept
    {
        packet_.data[1] = static_cast<std::uint8_t>( packet_.size - header_size );
        return packet_;
    }

private:
    Packet packet_{};
};

[[nodiscard]] fw::Result<Opcode> to_opcode( std::uint8_t value ) noexcept
{
    switch( static_cast<Opcode>( value ) ) {
    case Opcode::tare:
    case Opcode::start_measurement:
    case Opcode::stop_measurement:
    case Opcode::get_app_version:
    case Opcode::shutdown:
    case Opcode::sample_battery:
    case Opcode::get_progressor_id:
        return static_cast<Opcode>( value );
    }
    return std::unexpected( std::errc::not_supported );
}

} // namespace

fw::Result<Command> decode_command( std::span<const std::uint8_t> bytes ) noexcept
{
    if( bytes.empty() ) {
        return std::unexpected( std::errc::invalid_argument );
    }
    auto opcode = to_opcode( bytes[0] );
    if( !opcode ) {
        return std::unexpected( opcode.error() );
    }
    return Command{ .opcode = *opcode };
}

Packet encode_battery_voltage( std::uint32_t millivolts ) noexcept
{
    PacketWriter writer{ ResponseCode::command_response };
    writer.put_u32( millivolts );
    return writer.finish();
}

Packet encode_app_version( std::string_view version ) noexcept
{
    PacketWriter writer{ ResponseCode::command_response };
    for( char const c : version.substr( 0, max_payload_size ) ) {
        writer.put_u8( static_cast<std::uint8_t>( c ) );
    }
    return writer.finish();
}

Packet encode_progressor_id( const ProgressorId &id ) noexcept
{
    PacketWriter writer{ ResponseCode::command_response };
    for( std::uint8_t const byte : id | std::views::reverse ) {
        writer.put_u8( byte );
    }
    return writer.finish();
}

fw::Result<Packet> encode_weight_measurements( std::span<const Sample> samples ) noexcept
{
    if( samples.empty() || samples.size() > max_samples_per_packet ) {
        return std::unexpected( std::errc::invalid_argument );
    }
    PacketWriter writer{ ResponseCode::weight_measurement };
    for( const Sample &sample : samples ) {
        writer.put_f32( sample.kg );
        writer.put_u32( sample.timestamp_us );
    }
    return writer.finish();
}

Packet encode_low_power_warning() noexcept
{
    return PacketWriter{ ResponseCode::low_power_warning }.finish();
}

} // namespace progressor
