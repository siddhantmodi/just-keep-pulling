/**
 * @file gatt_codec.hpp
 * @brief Encoding and validation of the BLE service's characteristic values.
 *
 * Pure functions with no Zephyr dependency, so they are unit tested directly;
 * the GATT callbacks in ble_svc.cpp only forward to them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <span>

namespace ble_svc
{

/** @brief ATT error codes (Bluetooth Core spec, Vol 3, Part F, 3.4.1.1) returned to the client. */
enum class AttError : std::uint8_t {
    invalid_offset = 0x07,
    invalid_attribute_length = 0x0D,
    unlikely = 0x0E,
    value_not_allowed = 0x13,
};

/** @brief Decode a write to the LED characteristic: exactly one byte, 0 (off) or 1 (on), at offset 0. */
[[nodiscard]] constexpr std::expected<bool, AttError> decode_led_write( std::span<const std::uint8_t> value, std::uint16_t offset ) noexcept
{
    if( offset != 0 ) {
        return std::unexpected( AttError::invalid_offset );
    }
    if( value.size() != 1 ) {
        return std::unexpected( AttError::invalid_attribute_length );
    }
    if( value[0] > 1 ) {
        return std::unexpected( AttError::value_not_allowed );
    }
    return value[0] == 1;
}

[[nodiscard]] constexpr std::array<std::uint8_t, 1> encode_led_state( bool on ) noexcept
{
    return { static_cast<std::uint8_t>( on ? 1 : 0 ) };
}

/** @brief Encode a temperature in hundredths of a degree Celsius as int16 little-endian. */
[[nodiscard]] constexpr std::array<std::uint8_t, 2> encode_temperature( std::int16_t centicelsius ) noexcept
{
    auto const bits = static_cast<std::uint16_t>( centicelsius );
    return { static_cast<std::uint8_t>( bits & 0xFFU ), static_cast<std::uint8_t>( bits >> 8U ) };
}

} // namespace ble_svc
