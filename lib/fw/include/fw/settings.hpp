/**
 * @file settings.hpp
 * @brief fw::settings: typed load and save of plain-data values in Zephyr settings storage.
 *
 * Zephyr's settings API is callback-shaped (handlers, a read callback, C
 * linkage). These helpers load and save one value per key directly:
 * @code
 * if( auto factor = fw::settings::load<float>( "load_cell/factor" ) ) { ... }
 * (void)fw::settings::save( "load_cell/factor", 0.109F );
 * @endcode
 *
 * Values are stored as their bytes, so a stored value only loads into a type
 * of the same size. Call init() once at boot, before any load or save.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstddef>
#include <expected>
#include <fw/error.hpp>
#include <span>
#include <system_error>
#include <type_traits>

namespace fw::settings
{

/** @brief Types stored as their bytes: trivially copyable. */
template<typename T>
concept Storable = std::is_trivially_copyable_v<T>;

/** @brief Initialize the settings subsystem and its storage backend. */
[[nodiscard]] Result<> init() noexcept;

/**
 * @brief Load the value stored under @p key into @p out. Fails with
 *        no_such_file_or_directory if nothing is stored, and invalid_argument
 *        if the stored value is not exactly out.size() bytes.
 */
[[nodiscard]] Result<> load_bytes( const char *key, std::span<std::byte> out ) noexcept;

[[nodiscard]] Result<> save_bytes( const char *key, std::span<const std::byte> value ) noexcept;

/** @brief Delete the value stored under @p key. Deleting a missing key succeeds. */
[[nodiscard]] Result<> erase( const char *key ) noexcept;

/** @brief Load a value; see load_bytes() for the errors. */
template<Storable T>
[[nodiscard]] Result<T> load( const char *key ) noexcept
{
    T value{};
    if( auto ret = load_bytes( key, std::as_writable_bytes( std::span{ &value, 1 } ) ); !ret ) {
        return std::unexpected( ret.error() );
    }
    return value;
}

template<Storable T>
[[nodiscard]] Result<> save( const char *key, const T &value ) noexcept
{
    return save_bytes( key, std::as_bytes( std::span{ &value, 1 } ) );
}

} // namespace fw::settings
