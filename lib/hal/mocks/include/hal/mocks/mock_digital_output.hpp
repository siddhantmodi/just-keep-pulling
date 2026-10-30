/**
 * @file mock_digital_output.hpp
 * @brief hal::mocks::MockDigitalOutput: GoogleMock of hal::DigitalOutput, for
 *        injecting into components under test. Only on the include path when
 *        CONFIG_GTEST=y.
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

struct MockDigitalOutput {
    MOCK_METHOD( fw::Result<>, init, ( bool active ) );
    MOCK_METHOD( fw::Result<>, set, ( bool active ) );
};

static_assert( DigitalOutput<::testing::NiceMock<MockDigitalOutput>> );
static_assert( DigitalOutput<::testing::StrictMock<MockDigitalOutput>> );

} // namespace hal::mocks
