/**
 * @file identity.cpp
 * @brief identity_svc::Identity implementation and the ID's text form.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "identity.hpp"

#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <expected>
#include <fw/settings.hpp>
#include <ranges>
#include <system_error>

LOG_MODULE_REGISTER( identity_svc, CONFIG_APP_IDENTITY_SVC_LOG_LEVEL );

namespace identity_svc
{

namespace
{

/** @brief All-zero and all-0xFF IDs look like blank or erased storage. */
[[nodiscard]] bool is_usable( const DeviceId &id ) noexcept
{
    auto const all = [&id]( std::uint8_t value ) { return std::ranges::all_of( id, [value]( std::uint8_t byte ) { return byte == value; } ); };
    return !all( 0x00 ) && !all( 0xFF );
}

} // namespace

fw::Result<DeviceId> parse_device_id( std::string_view hex ) noexcept
{
    if( hex.size() != device_id_hex_length ) {
        return std::unexpected( std::errc::invalid_argument );
    }
    std::uint64_t value = 0;
    auto const [end, error] = std::from_chars( hex.begin(), hex.end(), value, 16 );
    if( error != std::errc{} || end != hex.end() ) {
        return std::unexpected( std::errc::invalid_argument );
    }
    DeviceId id{};
    for( std::uint8_t &byte : id | std::views::reverse ) {
        byte = static_cast<std::uint8_t>( value );
        value >>= 8U;
    }
    return id;
}

std::array<char, device_id_hex_length + 1> format_device_id( const DeviceId &id ) noexcept
{
    constexpr std::string_view digits = "0123456789ABCDEF";
    std::array<char, device_id_hex_length + 1> text{};
    for( std::size_t i = 0; i < id.size(); ++i ) {
        text[2 * i] = digits[id[i] >> 4];
        text[( 2 * i ) + 1] = digits[id[i] & 0x0F];
    }
    return text; // NUL-terminated: the last element stays 0
}

fw::Result<> Identity::init()
{
    auto saved = fw::settings::load<DeviceId>( id_key );
    if( saved && is_usable( *saved ) ) {
        id_ = *saved;
        LOG_INF( "device ID %s", format_device_id( id_ ).data() );
        return {};
    }
    if( !saved && saved.error() != std::errc::no_such_file_or_directory ) {
        LOG_WRN( "device ID unreadable (%d), generating a new one", fw::err::to_zephyr( saved.error() ) );
    }

    DeviceId generated{};
    do {
        sys_rand_get( generated.data(), generated.size() );
    } while( !is_usable( generated ) );

    id_ = generated;
    LOG_INF( "generated device ID %s", format_device_id( id_ ).data() );
    if( auto ret = fw::settings::save( id_key, generated ); !ret ) {
        LOG_ERR( "device ID not saved, a new one will be generated at the next boot: %d", fw::err::to_zephyr( ret.error() ) );
        return ret;
    }
    return {};
}

const DeviceId &Identity::id() const
{
    return id_;
}

fw::Result<> Identity::set_id( const DeviceId &id )
{
    if( !is_usable( id ) ) {
        return std::unexpected( std::errc::invalid_argument );
    }
    if( auto ret = fw::settings::save( id_key, id ); !ret ) {
        return ret;
    }
    LOG_INF( "device ID %s saved for the next boot", format_device_id( id ).data() );
    return {};
}

} // namespace identity_svc
