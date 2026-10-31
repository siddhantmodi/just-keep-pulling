/**
 * @file mock_battery_charger.hpp
 * @brief hal::mocks::MockBatteryCharger: GoogleMock of hal::IBatteryCharger.
 *        Only on the include path when CONFIG_GTEST=y.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fw/error.hpp>
#include <gmock/gmock.h>
#include <hal/battery_charger.hpp>

namespace hal::mocks
{

class MockBatteryCharger : public IBatteryCharger
{
public:
    MOCK_METHOD( fw::Result<>, init, (), ( override ) );
    MOCK_METHOD( fw::Result<BatteryStatus>, read, (), ( override ) );
};

} // namespace hal::mocks
