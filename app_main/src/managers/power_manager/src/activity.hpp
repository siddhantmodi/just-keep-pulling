/**
 * @file activity.hpp
 * @brief power_manager::Activity: the inactivity side of the power manager's logic.
 *
 * Counts idle time in steps of activity_check_interval: each check() without
 * activity adds one. Commands (except battery polls, as Crimpdeq), connections,
 * measurements and motion are activity, and nothing counts while a
 * measurement runs. After the idle timeout (longer while a client is
 * connected, as Crimpdeq), the client is disconnected and, at the next check,
 * the device sleeps until it is moved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "ble_svc.hpp"
#include "motion_svc.hpp"
#include "progressor.hpp"

#include <chrono>
#include <cstdint>
#include <fw/error.hpp>

namespace power_manager
{

inline constexpr std::chrono::seconds activity_check_interval{ 10 };

struct IdleTimeouts {
    std::chrono::seconds disconnected;
    std::chrono::seconds connected;
};

class Activity
{
public:
    constexpr Activity( motion_svc::IMotion &motion, ble_svc::IClient &client, IdleTimeouts timeouts ) noexcept
        : motion_{ motion }
        , client_{ client }
        , timeouts_{ timeouts }
    {
    }

    void on_command( progressor::Opcode opcode );
    void on_client_connection( bool connected );
    void on_measurement( bool measuring );

    /** @brief Every activity_check_interval: count idle time, and act on the timeout. Fails only if sleeping fails. */
    [[nodiscard]] fw::Result<> check();

    [[nodiscard]] std::chrono::seconds idle() const noexcept
    {
        return idle_;
    }

private:
    void reset();
    fw::Result<> sleep();

    motion_svc::IMotion &motion_;
    ble_svc::IClient &client_;
    IdleTimeouts timeouts_;
    std::chrono::seconds idle_{ 0 };
    bool connected_{ false };
    bool measuring_{ false };
    bool disconnecting_{ false }; // idle: the client was asked to disconnect; sleep at the next check
};

} // namespace power_manager
