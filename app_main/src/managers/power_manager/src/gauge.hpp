/**
 * @file gauge.hpp
 * @brief power_manager::Gauge: runs a hal::IFuelGauge and keeps its state across resets in settings.
 *
 * The first reading resumes from the saved state, or starts the gauge from
 * that reading. The state is saved every gauge_save_interval of readings and
 * by save(), before power goes (System OFF, ship mode), so a reset or the
 * power switch loses at most that much.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fw/error.hpp>
#include <hal/battery_charger.hpp>
#include <hal/fuel_gauge.hpp>
#include <span>

namespace power_manager
{

inline constexpr std::chrono::minutes gauge_save_interval{ 10 };
/** @brief Room for the gauge's state: nRF Fuel Gauge's is under 1 KB. */
inline constexpr std::size_t gauge_state_capacity = 1024;
inline constexpr const char *gauge_state_key = "power/gauge";

class Gauge
{
public:
    constexpr explicit Gauge( hal::IFuelGauge &gauge ) noexcept
        : gauge_{ gauge }
    {
    }

    /** @brief Take in a reading made @p elapsed after the previous one; the state of charge in % (0-100). */
    [[nodiscard]] fw::Result<std::uint8_t> update( const hal::BatteryStatus &status, std::chrono::milliseconds elapsed );

    /** @brief Save the state now. Nothing to save before the first reading. */
    [[nodiscard]] fw::Result<> save();

private:
    fw::Result<> start( const hal::BatteryStatus &status );

    hal::IFuelGauge &gauge_;
    bool started_{ false };
    std::chrono::milliseconds unsaved_{ 0 };
    std::array<std::byte, gauge_state_capacity> state_{};
};

} // namespace power_manager
