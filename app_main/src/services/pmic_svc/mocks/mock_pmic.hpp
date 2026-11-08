/**
 * @file mock_pmic.hpp
 * @brief pmic_svc::MockPmic: GoogleMock of pmic_svc::IPmic, for tests of its users.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "pmic_svc.hpp"

#include <fw/error.hpp>
#include <gmock/gmock.h>
#include <hal/battery_charger.hpp>

namespace pmic_svc
{

class MockPmic : public IPmic
{
public:
    MOCK_METHOD( fw::Result<>, init, (), ( override ) );
    MOCK_METHOD( fw::Result<hal::BatteryStatus>, read_battery, (), ( override ) );
    MOCK_METHOD( fw::Result<>, ship_mode, (), ( override ) );
};

} // namespace pmic_svc
