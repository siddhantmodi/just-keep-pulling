/**
 * @file sample_stats.hpp
 * @brief measurement_manager::SampleStats: a measurement's sample timing and spread, logged when it stops.
 *
 * For bring-up: the rate the ADC really ran at, gaps, and the noise and range
 * of the raw codes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "load_cell_svc.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace measurement_manager
{

class SampleStats
{
public:
    void add( const load_cell_svc::ForceSample &sample ) noexcept
    {
        ++count_;
        if( count_ == 1 ) {
            first_us_ = sample.timestamp_us;
            min_raw_ = sample.raw;
            max_raw_ = sample.raw;
        } else {
            max_gap_us_ = std::max( max_gap_us_, sample.timestamp_us - last_us_ );
            min_raw_ = std::min( min_raw_, sample.raw );
            max_raw_ = std::max( max_raw_, sample.raw );
        }
        last_us_ = sample.timestamp_us;

        // Welford's running mean and variance.
        double const delta = sample.raw - mean_raw_;
        mean_raw_ += delta / count_;
        m2_ += delta * ( sample.raw - mean_raw_ );
        kg_sum_ += static_cast<double>( sample.kg );
    }

    [[nodiscard]] std::uint32_t count() const noexcept
    {
        return count_;
    }

    [[nodiscard]] std::uint64_t max_gap_us() const noexcept
    {
        return max_gap_us_;
    }

    /** @brief From the first and last timestamps; 0 with fewer than two samples. */
    [[nodiscard]] double samples_per_second() const noexcept
    {
        if( count_ < 2 || last_us_ == first_us_ ) {
            return 0.0;
        }
        return ( count_ - 1 ) * 1e6 / static_cast<double>( last_us_ - first_us_ );
    }

    [[nodiscard]] double mean_raw() const noexcept
    {
        return mean_raw_;
    }

    /** @brief Sample standard deviation of the codes; 0 with fewer than two samples. */
    [[nodiscard]] double sd_raw() const noexcept
    {
        return count_ < 2 ? 0.0 : std::sqrt( m2_ / ( count_ - 1 ) );
    }

    [[nodiscard]] std::int32_t min_raw() const noexcept
    {
        return min_raw_;
    }

    [[nodiscard]] std::int32_t max_raw() const noexcept
    {
        return max_raw_;
    }

    [[nodiscard]] double mean_kg() const noexcept
    {
        return count_ == 0 ? 0.0 : kg_sum_ / count_;
    }

private:
    std::uint32_t count_{ 0 };
    std::uint64_t first_us_{ 0 };
    std::uint64_t last_us_{ 0 };
    std::uint64_t max_gap_us_{ 0 };
    std::int32_t min_raw_{ 0 };
    std::int32_t max_raw_{ 0 };
    double mean_raw_{ 0.0 };
    double m2_{ 0.0 };
    double kg_sum_{ 0.0 };
};

} // namespace measurement_manager
