/**
 * @file jkp_protocol.hpp
 * @brief The JustKeepPulling service's characteristic values: what the device
 *        sends and how client writes are checked.
 *
 * Our own service, beside the Progressor one, for what Tindeq's protocol has no
 * command for: calibration, the sample rate and the full device ID. Values are
 * little-endian, as in the Progressor protocol.
 *
 * Pure functions with no Zephyr dependency, so they are unit tested directly.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <fw/error.hpp>
#include <hal/load_cell_adc.hpp>
#include <limits>
#include <span>
#include <system_error>
#include <utility>

namespace ble_svc::jkp
{

/** @brief Calibration value: status (channels::CalibrationStatus), points, factor in g per ADC code (f32). */
inline constexpr std::size_t calibration_size = 6;
/** @brief Add calibration point write: the weight on the load cell in kg (f32). */
inline constexpr std::size_t calibration_kg_size = 4;
/** @brief Reset calibration write: this byte only, so a stray write can't reset by accident. */
inline constexpr std::uint8_t reset_calibration_value = 0x01;
/** @brief Sample rate value and write: samples per second (u16), one of hal::all_sample_rates. */
inline constexpr std::size_t sample_rate_size = 2;

[[nodiscard]] constexpr std::array<std::uint8_t, calibration_size> encode_calibration( std::uint8_t status, std::uint8_t points,
                                                                                       float factor_g_per_code ) noexcept
{
    auto const factor = std::bit_cast<std::uint32_t>( factor_g_per_code );
    return { status,
             points,
             static_cast<std::uint8_t>( factor ),
             static_cast<std::uint8_t>( factor >> 8U ),
             static_cast<std::uint8_t>( factor >> 16U ),
             static_cast<std::uint8_t>( factor >> 24U ) };
}

/** @brief The weight to calibrate with. Fails with message_size for a wrong length, invalid_argument for a negative or non-finite weight. */
[[nodiscard]] constexpr fw::Result<float> decode_calibration_kg( std::span<const std::uint8_t> value ) noexcept
{
    if( value.size() != calibration_kg_size ) {
        return std::unexpected( std::errc::message_size );
    }
    auto const bits = static_cast<std::uint32_t>( value[0] ) | ( static_cast<std::uint32_t>( value[1] ) << 8U ) |
                      ( static_cast<std::uint32_t>( value[2] ) << 16U ) | ( static_cast<std::uint32_t>( value[3] ) << 24U );
    auto const kg = std::bit_cast<float>( bits );
    // Also false for NaN.
    if( !( kg >= 0.0F && kg <= std::numeric_limits<float>::max() ) ) {
        return std::unexpected( std::errc::invalid_argument );
    }
    return kg;
}

/** @brief Fails with message_size for a wrong length, invalid_argument for any other byte than reset_calibration_value. */
[[nodiscard]] constexpr fw::Result<> decode_reset_calibration( std::span<const std::uint8_t> value ) noexcept
{
    if( value.size() != 1 ) {
        return std::unexpected( std::errc::message_size );
    }
    if( value[0] != reset_calibration_value ) {
        return std::unexpected( std::errc::invalid_argument );
    }
    return {};
}

[[nodiscard]] constexpr std::array<std::uint8_t, sample_rate_size> encode_sample_rate( hal::SampleRate rate ) noexcept
{
    auto const samples_per_second = std::to_underlying( rate );
    return { static_cast<std::uint8_t>( samples_per_second ), static_cast<std::uint8_t>( samples_per_second >> 8U ) };
}

/** @brief Fails with message_size for a wrong length, not_supported for a rate the ADC doesn't have. */
[[nodiscard]] constexpr fw::Result<hal::SampleRate> decode_sample_rate( std::span<const std::uint8_t> value ) noexcept
{
    if( value.size() != sample_rate_size ) {
        return std::unexpected( std::errc::message_size );
    }
    return hal::to_sample_rate( static_cast<std::uint16_t>( value[0] | ( value[1] << 8U ) ) );
}

} // namespace ble_svc::jkp
