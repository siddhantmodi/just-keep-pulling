/**
 * @file identity_svc.hpp
 * @brief Identity service public API: the device's unique 6-byte ID.
 *
 * The ID is generated randomly on first boot and saved in settings, so it
 * survives reboots and firmware updates. It is the Progressor ID and the
 * suffix of the advertised name. Debug builds can overwrite it from the shell.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <array>
#include <cstdint>
#include <fw/error.hpp>
#include <string_view>

namespace identity_svc
{

/** @brief Most significant byte first, as displayed. */
using DeviceId = std::array<std::uint8_t, 6>;

/** @brief Characters in a DeviceId written as hex, e.g. "7E4E17010A3C". */
inline constexpr std::size_t device_id_hex_length = 12;

/** @brief Parse 12 hex digits (either case). Fails with invalid_argument otherwise. */
[[nodiscard]] fw::Result<DeviceId> parse_device_id( std::string_view hex ) noexcept;

/** @brief Write the ID as 12 upper-case hex digits. */
[[nodiscard]] std::array<char, device_id_hex_length + 1> format_device_id( const DeviceId &id ) noexcept;

class IIdentity
{
public:
    /** @brief Load the saved ID, or generate and save one on first boot. */
    [[nodiscard]] virtual fw::Result<> init() = 0;

    [[nodiscard]] virtual const DeviceId &id() const = 0;

    /**
     * @brief Replace and save the ID. Fails with invalid_argument for all-zero
     *        or all-0xFF IDs. Users of the ID (e.g. advertising) pick it up on the next boot.
     */
    [[nodiscard]] virtual fw::Result<> set_id( const DeviceId &id ) = 0;

protected:
    // Never deleted through the interface: objects are static.
    ~IIdentity() = default;
};

/** @brief The app's identity. */
[[nodiscard]] IIdentity &identity();

} // namespace identity_svc
