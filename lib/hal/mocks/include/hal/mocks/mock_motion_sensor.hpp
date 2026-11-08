/**
 * @file mock_motion_sensor.hpp
 * @brief hal::mocks::MockMotionSensor: GoogleMock of hal::IMotionSensor.
 *        Only on the include path when CONFIG_GTEST=y.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fw/error.hpp>
#include <gmock/gmock.h>
#include <hal/motion_sensor.hpp>

namespace hal::mocks
{

class MockMotionSensor : public IMotionSensor
{
public:
    MOCK_METHOD( fw::Result<>, init, (), ( override ) );
    MOCK_METHOD( bool, moved, (), ( override ) );
};

} // namespace hal::mocks
