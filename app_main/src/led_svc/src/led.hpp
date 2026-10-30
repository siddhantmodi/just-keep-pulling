/**
 * @file led.hpp
 * @brief led_svc::Led: LED state logic over an injected hal::DigitalOutput.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <expected>
#include <fw/error.hpp>
#include <hal/digital_output.hpp>
#include <system_error>

namespace led_svc
{

template<hal::DigitalOutput Output>
class Led
{
public:
    constexpr explicit Led( Output &output ) noexcept
        : output_{ output }
    {
    }

    [[nodiscard]] fw::Result<> init()
    {
        if( auto ret = output_.init( false ); !ret ) {
            return ret;
        }
        on_ = false;
        initialized_ = true;
        return {};
    }

    [[nodiscard]] fw::Result<> set( bool on )
    {
        if( !initialized_ ) {
            return std::unexpected( std::errc::operation_not_permitted );
        }
        if( auto ret = output_.set( on ); !ret ) {
            return ret;
        }
        on_ = on;
        return {};
    }

    [[nodiscard]] fw::Result<> toggle()
    {
        return set( !on_ );
    }

    [[nodiscard]] bool is_on() const noexcept
    {
        return on_;
    }

private:
    Output &output_;
    bool initialized_{ false };
    bool on_{ false };
};

} // namespace led_svc
