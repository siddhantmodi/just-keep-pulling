/**
 * @file mock_motion.hpp
 * @brief motion_svc::MockMotion: GoogleMock of motion_svc::IMotion, for tests of its users.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "motion_svc.hpp"

#include <fw/error.hpp>
#include <gmock/gmock.h>

namespace motion_svc
{

class MockMotion : public IMotion
{
public:
    MOCK_METHOD( fw::Result<>, init, (), ( override ) );
    MOCK_METHOD( bool, moved, (), ( override ) );
    MOCK_METHOD( fw::Result<>, sleep_until_moved, (), ( override ) );
};

} // namespace motion_svc
