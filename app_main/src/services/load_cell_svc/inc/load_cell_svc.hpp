/**
 * @file load_cell_svc.hpp
 * @brief Load-cell service public API: the ILoadCell interface and the app's load cell.
 *
 * Samples the load cell and converts readings to kg (tare, calibration
 * factor). The factor and the configured sample rate are saved in settings
 * and loaded by init(); calibration points are kept in RAM only, as in Crimpdeq.
 *
 * Not thread-safe, apart from the configured sample rate: call from a single
 * thread (the measurement manager).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fw/error.hpp>

namespace load_cell_svc
{

struct ForceSample {
    float kg;
    /** Microseconds since boot when the ADC completed the conversion. */
    std::uint64_t timestamp_us;
};

class ILoadCell
{
public:
    /** @brief Check the ADC and load the saved factor and rate (the defaults if none, or if unusable). */
    [[nodiscard]] virtual fw::Result<> init() = 0;

    /** @brief The configured sampling rate, saved in settings (default 175 SPS). */
    [[nodiscard]] virtual std::uint16_t sample_rate() const = 0;

    /**
     * @brief Change and save the configured rate; it applies from the next
     *        start(). Fails with not_supported for a rate the ADC does not offer.
     *        Safe to call from another thread (e.g. the debug shell).
     */
    [[nodiscard]] virtual fw::Result<> set_sample_rate( std::uint16_t samples_per_second ) = 0;

    /** @brief Sample continuously. Fails with not_supported for a rate the ADC does not offer. */
    [[nodiscard]] virtual fw::Result<> start( std::uint16_t samples_per_second ) = 0;

    /** @brief Wait up to @p timeout for the next sample, in kg. See hal::ILoadCellAdc::read for the errors. */
    [[nodiscard]] virtual fw::Result<ForceSample> read( std::chrono::microseconds timeout ) = 0;

    [[nodiscard]] virtual fw::Result<> stop() = 0;

    /** @brief Samples lost since the last start because reads fell behind. */
    [[nodiscard]] virtual std::uint32_t lost_samples() const = 0;

    /**
     * @brief Zero the scale: the average reading over 200 ms becomes the
     *        no-load offset. Samples at the running rate, or briefly at the
     *        default rate when stopped.
     */
    [[nodiscard]] virtual fw::Result<> tare() = 0;

    /**
     * @brief Measure a calibration point under @p kg (average over 1.25 s at
     *        90 SPS), then restore the previous sampling. From the second point
     *        on, refit the factor and save it. Returns the number of points.
     *
     * Fails with invalid_argument for a negative or non-finite weight and
     * no_buffer_space after 20 points. A failed fit (see calibration::fit_factor)
     * clears the points and keeps the previous factor.
     */
    [[nodiscard]] virtual fw::Result<std::size_t> add_calibration_point( float kg ) = 0;

    /** @brief Clear the points and return to the default factor, erasing the saved one. */
    [[nodiscard]] virtual fw::Result<> reset_calibration() = 0;

    /** @brief Grams per ADC code. */
    [[nodiscard]] virtual float calibration_factor() const = 0;

protected:
    // Never deleted through the interface: objects are static.
    ~ILoadCell() = default;
};

/** @brief The app's load cell (devicetree alias load-cell-adc). */
[[nodiscard]] ILoadCell &load_cell();

} // namespace load_cell_svc
