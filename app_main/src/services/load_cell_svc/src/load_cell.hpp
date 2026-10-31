/**
 * @file load_cell.hpp
 * @brief load_cell_svc::LoadCell: tare, calibration and kg conversion over an injected hal::ILoadCellAdc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "calibration.hpp"
#include "load_cell_svc.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fw/error.hpp>
#include <hal/load_cell_adc.hpp>
#include <optional>

namespace load_cell_svc
{

/** @brief Settings key of the calibration factor. */
inline constexpr const char *factor_key = "load_cell/factor";
/** @brief Settings key of the configured sample rate. */
inline constexpr const char *rate_key = "load_cell/rate";

/** @brief The configured rate until changed, and the rate used to tare while not sampling (Crimpdeq's default). */
inline constexpr hal::SampleRate default_rate = hal::SampleRate::sps175;
inline constexpr std::chrono::milliseconds tare_duration{ 200 };

/** @brief Calibration points are averaged at a low rate, which has less noise per conversion. */
inline constexpr hal::SampleRate calibration_rate = hal::SampleRate::sps90;
inline constexpr std::chrono::milliseconds calibration_duration{ 1250 };
inline constexpr std::size_t max_calibration_points = 20;

class LoadCell final : public ILoadCell
{
public:
    constexpr explicit LoadCell( hal::ILoadCellAdc &adc ) noexcept
        : adc_{ adc }
    {
    }

    [[nodiscard]] fw::Result<> init() override;
    [[nodiscard]] hal::SampleRate sample_rate() const override;
    [[nodiscard]] fw::Result<> set_sample_rate( hal::SampleRate rate ) override;
    [[nodiscard]] fw::Result<> start( hal::SampleRate rate ) override;
    [[nodiscard]] fw::Result<ForceSample> read( std::chrono::microseconds timeout ) override;
    [[nodiscard]] fw::Result<> stop() override;
    [[nodiscard]] std::uint32_t lost_samples() const override;
    [[nodiscard]] fw::Result<> tare() override;
    [[nodiscard]] fw::Result<std::size_t> add_calibration_point( float kg ) override;
    [[nodiscard]] fw::Result<> reset_calibration() override;
    [[nodiscard]] float calibration_factor() const override;

private:
    /** @brief Average raw code over @p duration at @p rate, then restore the previous sampling. */
    fw::Result<double> average_raw( hal::SampleRate rate, std::chrono::milliseconds duration );

    hal::ILoadCellAdc &adc_;
    std::int32_t tare_{ 0 };
    float factor_{ calibration::default_factor };
    std::optional<hal::SampleRate> running_rate_; // empty: stopped
    std::atomic<hal::SampleRate> configured_rate_{ default_rate };
    std::array<calibration::Point, max_calibration_points> points_{};
    std::size_t point_count_{ 0 };
};

} // namespace load_cell_svc
