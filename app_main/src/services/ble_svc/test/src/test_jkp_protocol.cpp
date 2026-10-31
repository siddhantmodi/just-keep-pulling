/**
 * @file test_jkp_protocol.cpp
 * @brief The JustKeepPulling service's characteristic values.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "jkp_protocol.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>

namespace
{

using ble_svc::jkp::decode_calibration_kg;
using ble_svc::jkp::decode_reset_calibration;
using ble_svc::jkp::decode_sample_rate;
using ble_svc::jkp::encode_calibration;
using ble_svc::jkp::encode_sample_rate;

std::array<std::uint8_t, 4> f32_bytes( float value )
{
    auto const bits = std::bit_cast<std::uint32_t>( value );
    return { static_cast<std::uint8_t>( bits ), static_cast<std::uint8_t>( bits >> 8U ), static_cast<std::uint8_t>( bits >> 16U ),
             static_cast<std::uint8_t>( bits >> 24U ) };
}

TEST( JkpProtocol, EncodesTheCalibration )
{
    // 1.5f is 0x3FC00000.
    EXPECT_EQ( encode_calibration( 1, 3, 1.5F ), ( std::array<std::uint8_t, 6>{ 0x01, 0x03, 0x00, 0x00, 0xC0, 0x3F } ) );
}

TEST( JkpProtocol, DecodesACalibrationWeight )
{
    auto const bytes = f32_bytes( 20.5F );
    EXPECT_EQ( decode_calibration_kg( bytes ), 20.5F );
    auto const zero = f32_bytes( 0.0F );
    EXPECT_EQ( decode_calibration_kg( zero ), 0.0F );
}

TEST( JkpProtocol, RejectsAWeightOfTheWrongLength )
{
    constexpr std::array<std::uint8_t, 3> short_value{ 0x00, 0x00, 0xA4 };
    EXPECT_EQ( decode_calibration_kg( short_value ), std::unexpected( std::errc::message_size ) );
    constexpr std::array<std::uint8_t, 5> long_value{};
    EXPECT_EQ( decode_calibration_kg( long_value ), std::unexpected( std::errc::message_size ) );
}

TEST( JkpProtocol, RejectsNegativeAndNonFiniteWeights )
{
    for( float const kg :
         { -1.0F, std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() } ) {
        auto const bytes = f32_bytes( kg );
        EXPECT_EQ( decode_calibration_kg( bytes ), std::unexpected( std::errc::invalid_argument ) ) << kg;
    }
}

TEST( JkpProtocol, ResetTakesOnlyItsValue )
{
    constexpr std::array<std::uint8_t, 1> reset{ 0x01 };
    EXPECT_TRUE( decode_reset_calibration( reset ) );
    constexpr std::array<std::uint8_t, 1> other{ 0x00 };
    EXPECT_EQ( decode_reset_calibration( other ), std::unexpected( std::errc::invalid_argument ) );
    constexpr std::array<std::uint8_t, 2> too_long{ 0x01, 0x01 };
    EXPECT_EQ( decode_reset_calibration( too_long ), std::unexpected( std::errc::message_size ) );
    EXPECT_EQ( decode_reset_calibration( {} ), std::unexpected( std::errc::message_size ) );
}

TEST( JkpProtocol, EncodesTheSampleRate )
{
    EXPECT_EQ( encode_sample_rate( hal::SampleRate::sps1000 ), ( std::array<std::uint8_t, 2>{ 0xE8, 0x03 } ) );
}

TEST( JkpProtocol, DecodesEverySampleRate )
{
    for( hal::SampleRate const rate : hal::all_sample_rates ) {
        auto const bytes = encode_sample_rate( rate );
        EXPECT_EQ( decode_sample_rate( bytes ), rate );
    }
}

TEST( JkpProtocol, RejectsAnUnsupportedSampleRate )
{
    constexpr std::array<std::uint8_t, 2> hundred{ 100, 0 };
    EXPECT_EQ( decode_sample_rate( hundred ), std::unexpected( std::errc::not_supported ) );
    constexpr std::array<std::uint8_t, 1> short_value{ 90 };
    EXPECT_EQ( decode_sample_rate( short_value ), std::unexpected( std::errc::message_size ) );
}

} // namespace
