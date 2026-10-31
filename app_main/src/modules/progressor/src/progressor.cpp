/**
 * @file progressor.cpp
 * @brief Tindeq Progressor protocol encoding and decoding.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "progressor.hpp"

#include <bit>
#include <cmath>
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
constexpr std::size_t float_size = 4;

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

[[nodiscard]] bool takes_no_payload( Opcode opcode ) noexcept
{
    return opcode != Opcode::add_calibration_point;
}

[[nodiscard]] fw::Result<Opcode> to_opcode( std::uint8_t value ) noexcept
{
    switch( static_cast<Opcode>( value ) ) {
    case Opcode::tare:
    case Opcode::start_measurement:
    case Opcode::stop_measurement:
    case Opcode::add_calibration_point:
    case Opcode::get_app_version:
    case Opcode::shutdown:
    case Opcode::sample_battery:
    case Opcode::get_progressor_id:
        return static_cast<Opcode>( value );
    }
    return std::unexpected( std::errc::not_supported );
}

[[nodiscard]] float read_f32( std::span<const std::uint8_t, float_size> bytes ) noexcept
{
    std::uint32_t bits = 0;
    for( std::size_t i = 0; i < float_size; ++i ) {
        bits |= static_cast<std::uint32_t>( bytes[i] ) << ( 8 * i );
    }
    return std::bit_cast<float>( bits );
}

[[nodiscard]] fw::Result<float> decode_calibration_kg( std::span<const std::uint8_t> payload ) noexcept
{
    // Either the f32 straight after the opcode, or a length byte of 4 and then the f32.
    if( payload.size() == float_size + 1 && payload[0] == float_size ) {
        payload = payload.subspan( 1 );
    }
    if( payload.size() != float_size ) {
        return std::unexpected( std::errc::invalid_argument );
    }
    float const kg = read_f32( payload.first<float_size>() );
    if( !std::isfinite( kg ) || kg < 0.0F ) {
        return std::unexpected( std::errc::invalid_argument );
    }
    return kg;
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
    if( takes_no_payload( *opcode ) ) {
        return Command{ .opcode = *opcode, .calibration_kg = 0.0F };
    }
    auto kg = decode_calibration_kg( bytes.subspan( 1 ) );
    if( !kg ) {
        return std::unexpected( kg.error() );
    }
    return Command{ .opcode = *opcode, .calibration_kg = *kg };
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
