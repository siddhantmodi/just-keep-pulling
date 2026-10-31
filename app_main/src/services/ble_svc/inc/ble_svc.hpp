/**
 * @file ble_svc.hpp
 * @brief BLE service public API: the Tindeq Progressor GATT service, advertising, and the connected client.
 *
 * GATT service 7e4e1701-1ea6-40c9-9dcc-13d34ffead57 (Tindeq's):
 * - Data point, 7e4e1702-…: notify. Responses and weight measurements (progressor.hpp).
 * - Control point, 7e4e1703-…: write, write without response. Commands.
 *
 * Control point writes are decoded and published on channels::progressor_command;
 * connections and disconnections on channels::client_connection. Whoever
 * handles the commands answers through client().
 *
 * Open security, as on a Tindeq Progressor: no pairing, one connection.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <fw/error.hpp>
#include <span>

namespace ble_svc
{

/** @brief The connected client (e.g. the Tindeq app), as the code answering it sees it. All methods are thread-safe. */
class IClient
{
public:
    /**
     * @brief Send @p packet as a data point notification, without waiting.
     *
     * Fails with not_connected when no client is connected and subscribed,
     * and with no_buffer_space when too many notifications are still queued
     * (the link is slower than the data): the packet is dropped.
     */
    [[nodiscard]] virtual fw::Result<> send( std::span<const std::uint8_t> packet ) = 0;

    /** @brief The connection's ATT MTU (23, the minimum, when not connected). */
    [[nodiscard]] virtual std::uint16_t att_mtu() const = 0;

    /**
     * @brief Ask the client for connection parameters that suit streaming (a
     *        15 ms interval, no latency) or idling (30-50 ms, latency 4), as Crimpdeq does.
     */
    [[nodiscard]] virtual fw::Result<> set_streaming( bool streaming ) = 0;

    /** @brief Disconnect the client. Advertising restarts afterwards. Fails with not_connected. */
    [[nodiscard]] virtual fw::Result<> disconnect() = 0;

protected:
    // Never deleted through the interface: objects are static.
    ~IClient() = default;
};

/**
 * @brief Enable Bluetooth and advertise as Progressor_NNNN, NNNN taken from
 *        the device ID (identity_svc, initialized first). Restarts advertising
 *        after each disconnect.
 */
[[nodiscard]] fw::Result<> init();

/** @brief The connected client, or none (calls then fail with not_connected). */
[[nodiscard]] IClient &client();

} // namespace ble_svc
