/**
 * @file mock_fuel_gauge.hpp
 * @brief hal::mocks::MockFuelGauge: GoogleMock of hal::IFuelGauge.
 *        Only on the include path when CONFIG_GTEST=y.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <fw/error.hpp>
#include <gmock/gmock.h>
#include <hal/fuel_gauge.hpp>
#include <span>

namespace hal::mocks
{

class MockFuelGauge : public IFuelGauge
{
public:
    MOCK_METHOD( fw::Result<>, init, ( const BatteryStatus &status, std::span<const std::byte> saved_state ), ( override ) );
    MOCK_METHOD( fw::Result<float>, update, ( const BatteryStatus &status, std::chrono::milliseconds elapsed ), ( override ) );
    MOCK_METHOD( std::size_t, state_size, (), ( const, override ) );
    MOCK_METHOD( fw::Result<>, save_state, ( std::span<std::byte> out ), ( override ) );
};

} // namespace hal::mocks
