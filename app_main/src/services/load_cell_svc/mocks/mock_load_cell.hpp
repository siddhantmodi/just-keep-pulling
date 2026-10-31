/**
 * @file mock_load_cell.hpp
 * @brief load_cell_svc::MockLoadCell: GoogleMock of load_cell_svc::ILoadCell, for tests of its users.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "load_cell_svc.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fw/error.hpp>
#include <gmock/gmock.h>

namespace load_cell_svc
{

class MockLoadCell : public ILoadCell
{
public:
    MOCK_METHOD( fw::Result<>, init, (), ( override ) );
    MOCK_METHOD( hal::SampleRate, sample_rate, (), ( const, override ) );
    MOCK_METHOD( fw::Result<>, set_sample_rate, ( hal::SampleRate rate ), ( override ) );
    MOCK_METHOD( fw::Result<>, start, ( hal::SampleRate rate ), ( override ) );
    MOCK_METHOD( fw::Result<ForceSample>, read, ( std::chrono::microseconds timeout ), ( override ) );
    MOCK_METHOD( fw::Result<>, stop, (), ( override ) );
    MOCK_METHOD( std::uint32_t, lost_samples, (), ( const, override ) );
    MOCK_METHOD( fw::Result<>, tare, (), ( override ) );
    MOCK_METHOD( fw::Result<std::size_t>, add_calibration_point, ( float kg ), ( override ) );
    MOCK_METHOD( fw::Result<>, reset_calibration, (), ( override ) );
    MOCK_METHOD( float, calibration_factor, (), ( const, override ) );
};

} // namespace load_cell_svc
