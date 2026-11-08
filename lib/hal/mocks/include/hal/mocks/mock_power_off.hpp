/**
 * @file mock_power_off.hpp
 * @brief hal::mocks::MockPowerOff: GoogleMock of hal::IPowerOff.
 *        Only on the include path when CONFIG_GTEST=y.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fw/error.hpp>
#include <gmock/gmock.h>
#include <hal/power_off.hpp>

namespace hal::mocks
{

class MockPowerOff : public IPowerOff
{
public:
    MOCK_METHOD( fw::Result<>, init, (), ( override ) );
    MOCK_METHOD( fw::Result<>, power_off, (), ( override ) );
};

} // namespace hal::mocks
