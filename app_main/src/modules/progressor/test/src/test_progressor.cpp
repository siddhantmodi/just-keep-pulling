/**
 * @file test_progressor.cpp
 * @brief Unit tests for the Tindeq Progressor protocol encoding and decoding.
 *
 * Expected bytes are worked out by hand from the spec: little-endian, a code
 * byte, a length byte, then the payload.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "progressor.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <span>
#include <string>
#include <system_error>
#include <vector>

using progressor::Opcode;

namespace
{

std::vector<std::uint8_t> bytes_of( const progressor::Packet &packet )
{
    auto const view = packet.bytes();
    return { view.begin(), view.end() };
}

// --- decode_command -------------------------------------------------------

TEST( DecodeCommandTest, DecodesEachCommandWithoutPayload )
{
    constexpr std::array opcodes{ Opcode::tare,     Opcode::start_measurement, Opcode::stop_measurement, Opcode::get_app_version,
                                  Opcode::shutdown, Opcode::sample_battery,    Opcode::get_progressor_id };
    for( Opcode const opcode : opcodes ) {
        std::array<std::uint8_t, 1> const write{ static_cast<std::uint8_t>( opcode ) };

        auto command = progressor::decode_command( write );

        ASSERT_TRUE( command ) << "opcode " << static_cast<int>( opcode );
        EXPECT_EQ( command->opcode, opcode );
    }
}

TEST( DecodeCommandTest, AcceptsZeroLengthByteAndIgnoresTrailingBytes )
{
    std::array<std::uint8_t, 2> const with_length{ 0x65, 0x00 };
    std::array<std::uint8_t, 4> const with_trailing{ 0x66, 0x00, 0xAA, 0xBB };

    EXPECT_EQ( progressor::decode_command( with_length )->opcode, Opcode::start_measurement );
    EXPECT_EQ( progressor::decode_command( with_trailing )->opcode, Opcode::stop_measurement );
}

TEST( DecodeCommandTest, RejectsEmptyWrite )
{
    EXPECT_EQ( progressor::decode_command( std::span<const std::uint8_t>{} ).error(), std::errc::invalid_argument );
}

TEST( DecodeCommandTest, ReportsUnknownOpcodesAsNotSupported )
{
    // 0x67/0x68 are the undocumented peak-RFD commands; 0x00 is not a command at all.
    for( std::uint8_t const opcode : { 0x00, 0x67, 0x68, 0x6A, 0x72, 0xFF } ) {
        std::array<std::uint8_t, 1> const write{ opcode };

        EXPECT_EQ( progressor::decode_command( write ).error(), std::errc::not_supported ) << "opcode " << static_cast<int>( opcode );
    }
}

TEST( DecodeCommandTest, DecodesCalibrationWeightRightAfterOpcode )
{
    // 1.5 kg = 0x3FC00000
    std::array<std::uint8_t, 5> const write{ 0x69, 0x00, 0x00, 0xC0, 0x3F };

    auto command = progressor::decode_command( write );

    ASSERT_TRUE( command );
    EXPECT_EQ( command->opcode, Opcode::add_calibration_point );
    EXPECT_FLOAT_EQ( command->calibration_kg, 1.5F );
}

TEST( DecodeCommandTest, DecodesCalibrationWeightAfterLengthByte )
{
    std::array<std::uint8_t, 6> const write{ 0x69, 0x04, 0x00, 0x00, 0xC0, 0x3F };

    auto command = progressor::decode_command( write );

    ASSERT_TRUE( command );
    EXPECT_FLOAT_EQ( command->calibration_kg, 1.5F );
}

TEST( DecodeCommandTest, RejectsMalformedCalibrationWeight )
{
    std::array<std::uint8_t, 4> const too_short{ 0x69, 0x00, 0xC0, 0x3F };
    std::array<std::uint8_t, 5> const negative{ 0x69, 0x00, 0x00, 0xC0, 0xBF };     // -1.5
    std::array<std::uint8_t, 5> const not_a_number{ 0x69, 0x00, 0x00, 0xC0, 0x7F }; // NaN
    std::array<std::uint8_t, 5> const infinite{ 0x69, 0x00, 0x00, 0x80, 0x7F };     // +inf

    for( std::span<const std::uint8_t> const write : { std::span<const std::uint8_t>{ too_short }, std::span<const std::uint8_t>{ negative },
                                                       std::span<const std::uint8_t>{ not_a_number }, std::span<const std::uint8_t>{ infinite } } ) {
        EXPECT_EQ( progressor::decode_command( write ).error(), std::errc::invalid_argument );
    }
}

// --- encoders ---------------------------------------------------------------

TEST( EncodeTest, BatteryVoltage )
{
    // 3700 mV = 0x00000E74
    EXPECT_EQ( bytes_of( progressor::encode_battery_voltage( 3700 ) ), ( std::vector<std::uint8_t>{ 0x00, 0x04, 0x74, 0x0E, 0x00, 0x00 } ) );
}

TEST( EncodeTest, AppVersionIsAscii )
{
    EXPECT_EQ( bytes_of( progressor::encode_app_version( "2.0.4" ) ), ( std::vector<std::uint8_t>{ 0x00, 0x05, '2', '.', '0', '.', '4' } ) );
}

TEST( EncodeTest, AppVersionIsTruncatedToLargestPayload )
{
    std::string const long_version( 300, 'x' );

    auto const packet = progressor::encode_app_version( long_version );

    EXPECT_EQ( packet.size, progressor::max_packet_size );
    EXPECT_EQ( packet.data[1], progressor::max_packet_size - progressor::header_size );
}

TEST( EncodeTest, ProgressorIdIsLeastSignificantByteFirst )
{
    progressor::ProgressorId const id{ 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC };

    EXPECT_EQ( bytes_of( progressor::encode_progressor_id( id ) ), ( std::vector<std::uint8_t>{ 0x00, 0x06, 0xBC, 0x9A, 0x78, 0x56, 0x34, 0x12 } ) );
}

TEST( EncodeTest, WeightMeasurementIsFloatThenTimestampPerSample )
{
    // 1.5 kg = 0x3FC00000, 1000 us = 0x000003E8; -2 kg = 0xC0000000, 2000 us = 0x000007D0
    std::array<progressor::Sample, 2> const samples{ { { .kg = 1.5F, .timestamp_us = 1000 }, { .kg = -2.0F, .timestamp_us = 2000 } } };

    auto packet = progressor::encode_weight_measurements( samples );

    ASSERT_TRUE( packet );
    EXPECT_EQ( bytes_of( *packet ), ( std::vector<std::uint8_t>{ 0x01, 0x10, 0x00, 0x00, 0xC0, 0x3F, 0xE8, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0,
                                                                 0xD0, 0x07, 0x00, 0x00 } ) );
}

TEST( EncodeTest, WeightMeasurementTakesUpToThirtySamples )
{
    std::array<progressor::Sample, progressor::max_samples_per_packet + 1> samples{};

    auto full = progressor::encode_weight_measurements( std::span{ samples }.first( progressor::max_samples_per_packet ) );
    auto too_many = progressor::encode_weight_measurements( samples );
    auto none = progressor::encode_weight_measurements( std::span<const progressor::Sample>{} );

    ASSERT_TRUE( full );
    EXPECT_EQ( full->size, progressor::max_packet_size );
    EXPECT_EQ( too_many.error(), std::errc::invalid_argument );
    EXPECT_EQ( none.error(), std::errc::invalid_argument );
}

TEST( EncodeTest, LowPowerWarningHasNoPayload )
{
    EXPECT_EQ( bytes_of( progressor::encode_low_power_warning() ), ( std::vector<std::uint8_t>{ 0x04, 0x00 } ) );
}

// --- samples_per_packet -----------------------------------------------------

TEST( SamplesPerPacketTest, FitsNegotiatedMtu )
{
    EXPECT_EQ( progressor::samples_per_packet( 23 ), 2U );   // default ATT MTU: (23 - 5) / 8
    EXPECT_EQ( progressor::samples_per_packet( 185 ), 22U ); // common iOS MTU
    EXPECT_EQ( progressor::samples_per_packet( 247 ), 30U );
    EXPECT_EQ( progressor::samples_per_packet( 517 ), 30U ); // capped
    EXPECT_EQ( progressor::samples_per_packet( 0 ), 1U );
    EXPECT_EQ( progressor::samples_per_packet( 12 ), 1U );
}

static_assert( progressor::samples_per_packet( 247 ) * progressor::sample_size + progressor::header_size + 3 <= 247,
               "a full packet fits the largest MTU" );

} // namespace
