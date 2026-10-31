/**
 * @file app.cpp
 * @brief app_manager::App implementation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "app.hpp"

#include <expected>
#include <system_error>

namespace app_manager
{

fw::Result<> App::start()
{
    return publish_led_state();
}

fw::Result<> App::handle( const Event &event )
{
    switch( event.type ) {
    case Event::Type::led_request:
        return on_led_request( event.led_request );
    }
    return std::unexpected( std::errc::invalid_argument );
}

fw::Result<> App::on_led_request( const channels::LedRequest &request )
{
    auto applied = led_.set( request.on );
    if( auto published = publish_led_state(); applied && !published ) {
        return published;
    }
    return applied;
}

fw::Result<> App::publish_led_state()
{
    return channels::led_state.publish( { .on = led_.is_on() } );
}

} // namespace app_manager
