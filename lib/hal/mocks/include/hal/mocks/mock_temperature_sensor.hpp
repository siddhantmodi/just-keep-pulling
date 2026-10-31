/**
 * @file mock_temperature_sensor.hpp
 * @brief hal::mocks::MockTemperatureSensor: GoogleMock of hal::ITemperatureSensor.
 *        Only on the include path when CONFIG_GTEST=y.
 *
 * Unconfigured calls succeed; read_millicelsius() then returns 0.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <fw/error.hpp>
#include <gmock/gmock.h>
#include <hal/temperature_sensor.hpp>

namespace hal::mocks
{

class MockTemperatureSensor : public ITemperatureSensor
{
public:
    MOCK_METHOD( fw::Result<>, init, (), ( override ) );
    MOCK_METHOD( fw::Result<std::int32_t>, read_millicelsius, (), ( override ) );
};

} // namespace hal::mocks
