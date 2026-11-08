/**
 * @file fuel_gauge.hpp
 * @brief hal::IFuelGauge: the capability of estimating the battery's state of charge from its readings.
 *
 * No Zephyr includes: logic that depends on this stays independent of the
 * hardware.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <fw/error.hpp>
#include <hal/battery_charger.hpp>
#include <span>

namespace hal
{

class IFuelGauge
{
public:
    /**
     * @brief Start from the reading @p status, or resume from @p saved_state
     *        (as save_state() wrote it; empty to start fresh). Fails with
     *        invalid_argument for a saved state it cannot use.
     */
    [[nodiscard]] virtual fw::Result<> init( const BatteryStatus &status, std::span<const std::byte> saved_state ) = 0;

    /** @brief Take in a reading made @p elapsed after the previous one (0 for the first); the state of charge in % (0-100). */
    [[nodiscard]] virtual fw::Result<float> update( const BatteryStatus &status, std::chrono::milliseconds elapsed ) = 0;

    /** @brief The bytes save_state() writes; 0 if it keeps no state. */
    [[nodiscard]] virtual std::size_t state_size() const = 0;

    /** @brief Write the state to @p out (state_size() bytes), for init() after a reset. */
    [[nodiscard]] virtual fw::Result<> save_state( std::span<std::byte> out ) = 0;

protected:
    // Never deleted through the interface: objects are static.
    ~IFuelGauge() = default;
};

} // namespace hal
