/**
 * @file calibration.hpp
 * @brief Load-cell calibration maths: converting ADC codes to kg and fitting the factor.
 *
 * Pure functions, no Zephyr. Matches Crimpdeq v3: a reading is
 * (raw - tare) x factor / 1000 kg, with the factor in grams per ADC code, and
 * the factor is a least-squares fit of averaged (raw, kg) calibration points.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <fw/error.hpp>
#include <span>

namespace calibration
{

/** @brief An averaged, untared ADC code and the known load it was measured under. */
struct Point {
    double raw;
    float kg;
};

/**
 * @brief Grams per ADC code for the ADS1220 at gain 128, ratiometric, with the
 *        WH-C100 crane-scale load cell (Crimpdeq's default; same hardware).
 */
inline constexpr float default_factor = 0.109F;

/** @brief Factors more than this many times above or below the default are rejected as implausible. */
inline constexpr float factor_tolerance = 10.0F;

/** @brief Either sign is accepted, so a load cell wired in reverse can still be calibrated. */
[[nodiscard]] bool is_plausible( float factor ) noexcept;

[[nodiscard]] constexpr float to_kg( std::int32_t raw, std::int32_t tare, float factor ) noexcept
{
    return static_cast<float>( static_cast<std::int64_t>( raw ) - tare ) * factor / 1000.0F;
}

/**
 * @brief Least-squares factor for calibration points.
 *
 * Measurements are tared with no load, so the line passes through the
 * no-load reading: the mean of the zero-kg points, or the mean of all points
 * when there are none.
 *
 * Fails with invalid_argument for fewer than two points or points with
 * (nearly) the same raw code, and result_out_of_range for an implausible factor.
 */
[[nodiscard]] fw::Result<float> fit_factor( std::span<const Point> points ) noexcept;

} // namespace calibration
