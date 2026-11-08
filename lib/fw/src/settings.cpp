/**
 * @file settings.cpp
 * @brief fw::settings over Zephyr settings: one value per key, loaded directly.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/settings/settings.h>

#include <cerrno>
#include <fw/settings.hpp>
#include <utility>

namespace fw::settings
{

namespace
{

struct LoadContext {
    std::span<std::byte> out;
    bool found;
    int error;
};

// Called for the key itself and for any keys below it; only the key itself is wanted.
// The result goes through the context: settings_load_subtree_direct() ignores a callback's
// return value for some backends.
int on_value( const char *key, size_t len, settings_read_cb read_cb, void *cb_arg, void *param )
{
    auto &context = *static_cast<LoadContext *>( param );
    const char *next = nullptr;

    if( settings_name_next( key, &next ) != 0 || len == 0 ) {
        return 0; // a key below this one, or a deleted value
    }
    context.found = true;
    if( len != context.out.size() ) {
        context.error = -EINVAL;
        return 0;
    }
    ssize_t const read = read_cb( cb_arg, context.out.data(), len );
    if( std::cmp_not_equal( read, len ) ) {
        context.error = read < 0 ? static_cast<int>( read ) : -EIO;
    }
    return 0;
}

} // namespace

Result<> init() noexcept
{
    return err::check( settings_subsys_init() );
}

Result<> load_bytes( const char *key, std::span<std::byte> out ) noexcept
{
    LoadContext context{ .out = out, .found = false, .error = 0 };
    if( auto ret = err::check( settings_load_subtree_direct( key, on_value, &context ) ); !ret ) {
        return ret;
    }
    if( !context.found ) {
        return std::unexpected( std::errc::no_such_file_or_directory );
    }
    return err::check( context.error );
}

Result<> save_bytes( const char *key, std::span<const std::byte> value ) noexcept
{
    return err::check( settings_save_one( key, value.data(), value.size() ) );
}

Result<> erase( const char *key ) noexcept
{
    return err::check( settings_delete( key ) );
}

} // namespace fw::settings
