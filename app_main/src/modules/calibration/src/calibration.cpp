/**
 * @file calibration.cpp
 * @brief Load-cell calibration fit (port of Crimpdeq v3's fit_calibration_factor).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "calibration.hpp"

#include <cmath>
#include <expected>
#include <limits>
#include <system_error>

namespace calibration
{

namespace
{

struct Mean {
    double raw;
    double grams;
};

/** @brief Mean of the points, in grams, optionally only the zero-kg ones. Fails with no_message if none qualify. */
fw::Result<Mean> mean( std::span<const Point> points, bool zero_kg_only ) noexcept
{
    double sum_raw = 0.0;
    double sum_grams = 0.0;
    std::size_t count = 0;
    for( const Point &point : points ) {
        if( zero_kg_only && point.kg != 0.0F ) {
            continue;
        }
        sum_raw += point.raw;
        sum_grams += static_cast<double>( point.kg ) * 1000.0;
        ++count;
    }
    if( count == 0 ) {
        return std::unexpected( std::errc::no_message );
    }
    return Mean{ .raw = sum_raw / static_cast<double>( count ), .grams = sum_grams / static_cast<double>( count ) };
}

} // namespace

bool is_plausible( float factor ) noexcept
{
    float const magnitude = std::fabs( factor );
    return magnitude >= default_factor / factor_tolerance && magnitude <= default_factor * factor_tolerance;
}

fw::Result<float> fit_factor( std::span<const Point> points ) noexcept
{
    if( points.size() < 2 ) {
        return std::unexpected( std::errc::invalid_argument );
    }

    auto anchor = mean( points, true );
    if( !anchor ) {
        anchor = mean( points, false );
    }

    // Least-squares slope of a line through the anchor, in grams per code.
    double sum_raw_grams = 0.0;
    double sum_raw_squared = 0.0;
    for( const Point &point : points ) {
        double const raw = point.raw - anchor->raw;
        double const grams = ( static_cast<double>( point.kg ) * 1000.0 ) - anchor->grams;
        sum_raw_grams += raw * grams;
        sum_raw_squared += raw * raw;
    }
    if( sum_raw_squared < std::numeric_limits<double>::epsilon() ) {
        return std::unexpected( std::errc::invalid_argument );
    }

    auto const factor = static_cast<float>( sum_raw_grams / sum_raw_squared );
    if( !is_plausible( factor ) ) {
        return std::unexpected( std::errc::result_out_of_range );
    }
    return factor;
}

} // namespace calibration
