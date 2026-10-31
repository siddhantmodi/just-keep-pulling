/**
 * @file mock_led.hpp
 * @brief led_svc::MockLed: GoogleMock of led_svc::ILed, for tests of its users.
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
    MOCK_METHOD( fw::Result<>, show, ( Color color ), ( override ) );
    MOCK_METHOD( Color, color, (), ( const, override ) );
};

} // namespace led_svc
