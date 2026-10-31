/**
 * @file indicator.cpp
 * @brief power_manager::Indicator implementation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "indicator.hpp"

namespace power_manager
{

using led_svc::Color;

fw::Result<> Indicator::start()
{
    return show();
}

fw::Result<> Indicator::on_client_connection( bool connected )
{
    connected_ = connected;
    return show();
}

fw::Result<> Indicator::on_battery( std::uint32_t millivolts )
{
    bool const low = millivolts <= low_battery_led_mv;
    if( low && !low_battery_ ) {
        step_ = 0; // start a blink with the LED on
    }
    low_battery_ = low;
    return show();
}

fw::Result<> Indicator::on_sleep( bool sleeping )
{
    sleeping_ = sleeping;
    step_ = 0;
    return show();
}

fw::Result<> Indicator::on_blink_step()
{
    step_ = ( step_ + 1 ) % blink_steps;
    return show();
}

fw::Result<> Indicator::show()
{
    Color color = connected_ ? Color::green : Color::blue;
    if( sleeping_ ) {
        color = Color::off;
    } else if( low_battery_ ) {
        color = step_ == 0 ? Color::red : Color::off;
    }
    if( color == led_.color() ) {
        return {};
    }
    return led_.show( color );
}

} // namespace power_manager
