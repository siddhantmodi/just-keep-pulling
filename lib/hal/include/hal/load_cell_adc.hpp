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

#include <chrono>
#include <cstdint>
#include <fw/error.hpp>

namespace hal
{

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

    /** @brief Whether start() accepts @p samples_per_second. */
    [[nodiscard]] virtual bool supports_rate( std::uint16_t samples_per_second ) const = 0;

    /** @brief Convert continuously. Fails with not_supported for a rate the ADC does not offer. */
    [[nodiscard]] virtual fw::Result<> start( std::uint16_t samples_per_second ) = 0;

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
