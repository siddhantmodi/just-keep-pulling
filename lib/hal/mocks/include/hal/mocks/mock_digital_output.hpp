/**
 * @file mock_digital_output.hpp
 * @brief hal::mocks::MockDigitalOutput: GoogleMock of hal::IDigitalOutput.
 *        Only on the include path when CONFIG_GTEST=y.
 *
 * Unconfigured calls return success (a default-constructed fw::Result<>).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fw/error.hpp>
#include <gmock/gmock.h>
#include <hal/digital_output.hpp>

namespace hal::mocks
{

class MockDigitalOutput : public IDigitalOutput
{
public:
    MOCK_METHOD( fw::Result<>, init, ( bool active ), ( override ) );
    MOCK_METHOD( fw::Result<>, set, ( bool active ), ( override ) );
};

} // namespace hal::mocks
