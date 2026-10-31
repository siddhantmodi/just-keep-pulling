/**
 * @file power.hpp
 * @brief power_manager::Power: the power manager's logic, with no thread or timer of its own.
 *
 * The manager thread (power_manager.cpp) hands it Events and, after each,
 * schedules the next battery check after next_check(). Tests call handle()
 * directly.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "ble_svc.hpp"
#include "pmic_svc.hpp"

#include <chrono>
#include <cstdint>
#include <fw/error.hpp>
#include <hal/battery_charger.hpp>
#include <type_traits>

namespace power_manager
{

/** @brief At or below this, on battery alone, the device warns and powers off (Crimpdeq's: the 3.3 V rail drops out below). */
inline constexpr std::uint32_t low_battery_mv = 3300;

inline constexpr std::chrono::milliseconds check_interval{ 30'000 };
/** @brief A low reading is confirmed by another this much later, so a load transient (e.g. a radio burst) doesn't power off. */
inline constexpr std::chrono::milliseconds confirm_delay{ 1'000 };
/** @brief Time for the low-power warning to reach the client before power goes (Crimpdeq's). */
inline constexpr std::chrono::milliseconds warning_delay{ 500 };

/** @brief Whether @p status calls for powering off: low, and nothing charging or powering the device. */
[[nodiscard]] constexpr bool is_low( const hal::BatteryStatus &status ) noexcept
{
    return status.millivolts <= low_battery_mv && !status.charging && !status.external_power;
}

/** @brief One thing for the manager to handle. Only the battery check so far; idle timeouts will join it. */
struct Event {
    enum class Type : std::uint8_t {
        battery_check,
    };

    Type type;
};

static_assert( std::is_trivially_copyable_v<Event>, "events are copied through a k_msgq" );

class Power
{
public:
    constexpr Power( pmic_svc::IPmic &pmic, ble_svc::IClient &client ) noexcept
        : pmic_{ pmic }
        , client_{ client }
    {
    }

    [[nodiscard]] fw::Result<> handle( const Event &event );

    /** @brief When to check the battery next: the interval, or sooner to confirm a low reading or power off. */
    [[nodiscard]] std::chrono::milliseconds next_check() const noexcept
    {
        return next_check_;
    }

private:
    enum class State : std::uint8_t {
        monitoring,
        confirming_low, // one low reading so far
        powering_off,   // warning sent; ship mode at the next check
    };

    fw::Result<> check_battery();
    fw::Result<> power_off();
    void monitor();

    pmic_svc::IPmic &pmic_;
    ble_svc::IClient &client_;
    State state_{ State::monitoring };
    std::chrono::milliseconds next_check_{ check_interval };
};

} // namespace power_manager
