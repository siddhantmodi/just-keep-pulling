/**
 * @file load_cell_adc.hpp
 * @brief hal::ILoadCellAdc: the capability of sampling a load cell (bridge) continuously.
 *
 * No Zephyr includes: logic that depends on this stays independent of the
 * hardware. Samples are raw signed ADC codes; converting them to force is the
 * caller's job (tare, calibration).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <expected>
#include <fw/error.hpp>
#include <system_error>
#include <utility>

namespace hal
{

/**
 * @brief The sampling rates the load-cell ADC offers (the ADS1220's normal-mode
 *        rates). The value is the rate in samples per second.
 */
enum class SampleRate : std::uint16_t {
    sps20 = 20,
    sps45 = 45,
    sps90 = 90,
    sps175 = 175,
    sps330 = 330,
    sps600 = 600,
    sps1000 = 1000,
};

inline constexpr std::array all_sample_rates{ SampleRate::sps20,  SampleRate::sps45,  SampleRate::sps90,  SampleRate::sps175,
                                              SampleRate::sps330, SampleRate::sps600, SampleRate::sps1000 };

[[nodiscard]] constexpr std::uint16_t samples_per_second( SampleRate rate ) noexcept
{
    return std::to_underlying( rate );
}

/** @brief The rate for a number of samples per second, e.g. from settings or a user. Fails with not_supported. */
[[nodiscard]] constexpr fw::Result<SampleRate> to_sample_rate( std::uint16_t samples_per_second ) noexcept
{
    for( SampleRate const rate : all_sample_rates ) {
        if( std::to_underlying( rate ) == samples_per_second ) {
            return rate;
        }
    }
    return std::unexpected( std::errc::not_supported );
}

struct LoadCellSample {
    std::int32_t raw;
    /** Microseconds since boot when the conversion completed (taken in its interrupt, not when read). */
    std::uint64_t timestamp_us;
};

class ILoadCellAdc
{
public:
    /** @brief Check the ADC is usable. It stays powered down until start(). Fails with no_such_device. */
    [[nodiscard]] virtual fw::Result<> init() = 0;

    /** @brief Convert continuously at @p rate. */
    [[nodiscard]] virtual fw::Result<> start( SampleRate rate ) = 0;

    /**
     * @brief Wait up to @p timeout for the next sample. Fails with
     *        resource_unavailable_try_again on timeout and operation_not_permitted when not started.
     */
    [[nodiscard]] virtual fw::Result<LoadCellSample> read( std::chrono::microseconds timeout ) = 0;

    /** @brief Samples overwritten before they were read, since the last start: the reader fell behind. */
    [[nodiscard]] virtual std::uint32_t lost_samples() const = 0;

    /** @brief Stop converting and power down. */
    [[nodiscard]] virtual fw::Result<> stop() = 0;

protected:
    // Never deleted through the interface: objects are static.
    ~ILoadCellAdc() = default;
};

} // namespace hal
