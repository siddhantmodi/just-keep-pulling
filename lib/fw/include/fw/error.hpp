/**
 * @file error.hpp
 * @brief Error model: functions that can fail return fw::Result<T>, which is a
 *        std::expected carrying a std::errc on failure.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <expected>
#include <system_error>

namespace fw
{

/** @brief A value of type T, or a std::errc describing why it could not be produced. */
template<typename T = void>
using Result = std::expected<T, std::errc>;

namespace err
{

/** @brief Convert a negative Zephyr/POSIX return code (e.g. -EIO) to std::errc. */
[[nodiscard]] constexpr std::errc from_zephyr( int rc ) noexcept
{
    return std::errc{ -rc };
}

/** @brief Convert std::errc back to a negative Zephyr return code, e.g. for logging. */
[[nodiscard]] constexpr int to_zephyr( std::errc error ) noexcept
{
    return -static_cast<int>( error );
}

/** @brief Map a Zephyr return code to a Result: negative is an error, anything else is success. */
[[nodiscard]] constexpr Result<> check( int rc ) noexcept
{
    if( rc < 0 ) {
        return std::unexpected( from_zephyr( rc ) );
    }
    return {};
}

} // namespace err

} // namespace fw
