/**
 * @file advertising.hpp
 * @brief The advertised name, derived from the device ID.
 *
 * Pure functions with no Zephyr dependency, so they are unit tested directly.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "identity_svc.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ble_svc
{

inline constexpr std::string_view name_prefix = "Progressor_";
inline constexpr std::size_t name_digits = 4;
/** @brief Including the terminating NUL. */
inline constexpr std::size_t name_size = name_prefix.size() + name_digits + 1;

/**
 * @brief "Progressor_NNNN", the name Tindeq's app looks for, as on a real
 *        Progressor: NNNN is the ID's last two bytes, as a decimal number modulo
 *        10000. The full ID is the Progressor ID, so two devices that share
 *        a name are still told apart.
 */
[[nodiscard]] constexpr std::array<char, name_size> advertised_name( const identity_svc::DeviceId &id ) noexcept
{
    std::array<char, name_size> name{};
    for( std::size_t i = 0; i < name_prefix.size(); ++i ) {
        name[i] = name_prefix[i];
    }
    unsigned number = ( ( static_cast<unsigned>( id[id.size() - 2] ) << 8U ) | id[id.size() - 1] ) % 10000U;
    for( std::size_t i = name_size - 2; i >= name_prefix.size(); --i ) {
        name[i] = static_cast<char>( '0' + ( number % 10U ) );
        number /= 10U;
    }
    return name;
}

} // namespace ble_svc
