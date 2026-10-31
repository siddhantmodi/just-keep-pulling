/**
 * @file mock_led.hpp
 * @brief led_svc::MockLed: GoogleMock of led_svc::ILed, for tests of components that use the LED.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "led_svc.hpp"

#include <fw/error.hpp>
#include <gmock/gmock.h>

namespace led_svc
{

class MockLed : public ILed
{
public:
    MOCK_METHOD( fw::Result<>, init, (), ( override ) );
    MOCK_METHOD( fw::Result<>, set, ( bool on ), ( override ) );
    MOCK_METHOD( bool, is_on, (), ( const, override ) );
};

} // namespace led_svc
