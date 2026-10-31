/**
 * @file mock_load_cell_adc.hpp
 * @brief hal::mocks::MockLoadCellAdc: GoogleMock of hal::ILoadCellAdc.
 *        Only on the include path when CONFIG_GTEST=y.
 *
 * Unconfigured calls succeed; read() then returns a zero sample at time 0.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <fw/error.hpp>
#include <gmock/gmock.h>
#include <hal/load_cell_adc.hpp>

namespace hal::mocks
{

class MockLoadCellAdc : public ILoadCellAdc
{
public:
    MOCK_METHOD( fw::Result<>, init, (), ( override ) );
    MOCK_METHOD( bool, supports_rate, ( std::uint16_t samples_per_second ), ( const, override ) );
    MOCK_METHOD( fw::Result<>, start, ( std::uint16_t samples_per_second ), ( override ) );
    MOCK_METHOD( fw::Result<LoadCellSample>, read, ( std::chrono::microseconds timeout ), ( override ) );
    MOCK_METHOD( std::uint32_t, lost_samples, (), ( const, override ) );
    MOCK_METHOD( fw::Result<>, stop, (), ( override ) );
};

} // namespace hal::mocks
