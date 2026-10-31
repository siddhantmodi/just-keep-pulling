/**
 * @file test_gatt_codec.cpp
 * @brief Unit tests for the BLE service's characteristic encoding and validation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "gatt_codec.hpp"

#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <span>

using ble_svc::AttError;

namespace
{

TEST( DecodeLedWriteTest, AcceptsZeroAndOne )
{
    constexpr std::array<std::uint8_t, 1> off{ 0 };
    constexpr std::array<std::uint8_t, 1> on{ 1 };

    EXPECT_EQ( ble_svc::decode_led_write( off, 0 ), false );
    EXPECT_EQ( ble_svc::decode_led_write( on, 0 ), true );
}

TEST( DecodeLedWriteTest, RejectsOtherValues )
{
    constexpr std::array<std::uint8_t, 1> two{ 2 };
    constexpr std::array<std::uint8_t, 1> max{ 0xFF };

    EXPECT_EQ( ble_svc::decode_led_write( two, 0 ).error(), AttError::value_not_allowed );
    EXPECT_EQ( ble_svc::decode_led_write( max, 0 ).error(), AttError::value_not_allowed );
}

TEST( DecodeLedWriteTest, RejectsWrongLength )
{
    constexpr std::array<std::uint8_t, 2> two_bytes{ 1, 0 };

    EXPECT_EQ( ble_svc::decode_led_write( std::span<const std::uint8_t>{}, 0 ).error(), AttError::invalid_attribute_length );
    EXPECT_EQ( ble_svc::decode_led_write( two_bytes, 0 ).error(), AttError::invalid_attribute_length );
}

TEST( DecodeLedWriteTest, RejectsNonZeroOffset )
{
    constexpr std::array<std::uint8_t, 1> on{ 1 };

    EXPECT_EQ( ble_svc::decode_led_write( on, 1 ).error(), AttError::invalid_offset );
}

TEST( EncodeTest, LedState )
{
    EXPECT_EQ( ble_svc::encode_led_state( false ), ( std::array<std::uint8_t, 1>{ 0 } ) );
    EXPECT_EQ( ble_svc::encode_led_state( true ), ( std::array<std::uint8_t, 1>{ 1 } ) );
}

TEST( EncodeTest, TemperatureIsLittleEndianInt16 )
{
    EXPECT_EQ( ble_svc::encode_temperature( 2537 ), ( std::array<std::uint8_t, 2>{ 0xE9, 0x09 } ) );
    EXPECT_EQ( ble_svc::encode_temperature( -1 ), ( std::array<std::uint8_t, 2>{ 0xFF, 0xFF } ) );
    EXPECT_EQ( ble_svc::encode_temperature( std::numeric_limits<std::int16_t>::min() ), ( std::array<std::uint8_t, 2>{ 0x00, 0x80 } ) );
}

} // namespace
