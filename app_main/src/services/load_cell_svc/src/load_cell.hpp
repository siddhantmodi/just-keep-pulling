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
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fw/error.hpp>
#include <hal/load_cell_adc.hpp>

namespace load_cell_svc
{

/** @brief Settings key of the calibration factor. */
inline constexpr const char *factor_key = "load_cell/factor";

/** @brief Rate used to tare while not sampling (Crimpdeq's default rate). */
inline constexpr std::uint16_t default_samples_per_second = 175;
inline constexpr std::chrono::milliseconds tare_duration{ 200 };

/** @brief Calibration points are averaged at a low rate, which has less noise per conversion. */
inline constexpr std::uint16_t calibration_samples_per_second = 90;
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
    [[nodiscard]] fw::Result<> start( std::uint16_t samples_per_second ) override;
    [[nodiscard]] fw::Result<ForceSample> read( std::chrono::microseconds timeout ) override;
    [[nodiscard]] fw::Result<> stop() override;
    [[nodiscard]] std::uint32_t lost_samples() const override;
    [[nodiscard]] fw::Result<> tare() override;
    [[nodiscard]] fw::Result<std::size_t> add_calibration_point( float kg ) override;
    [[nodiscard]] fw::Result<> reset_calibration() override;
    [[nodiscard]] float calibration_factor() const override;

private:
    /** @brief Average raw code over @p duration at @p samples_per_second, then restore the previous sampling. */
    fw::Result<double> average_raw( std::uint16_t samples_per_second, std::chrono::milliseconds duration );

    hal::ILoadCellAdc &adc_;
    std::int32_t tare_{ 0 };
    float factor_{ calibration::default_factor };
    std::uint16_t running_rate_{ 0 }; // 0: stopped
    std::array<calibration::Point, max_calibration_points> points_{};
    std::size_t point_count_{ 0 };
};

} // namespace load_cell_svc
