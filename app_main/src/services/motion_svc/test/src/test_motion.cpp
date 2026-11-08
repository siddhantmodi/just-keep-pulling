/**
 * @file test_motion.cpp
 * @brief Unit tests of motion_svc::Motion against a mocked motion sensor and power switch.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "motion.hpp"

#include <expected>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <hal/mocks/mock_motion_sensor.hpp>
#include <hal/mocks/mock_power_off.hpp>
#include <system_error>

namespace
{

using testing::Return;

class MotionTest : public testing::Test
{
protected:
    testing::StrictMock<hal::mocks::MockMotionSensor> sensor_;
    testing::StrictMock<hal::mocks::MockPowerOff> system_off_;
    motion_svc::Motion motion_{ sensor_, system_off_ };
};

TEST_F( MotionTest, InitStartsTheSensorAndChecksTheWakePin )
{
    EXPECT_CALL( sensor_, init() ).WillOnce( Return( fw::Result<>{} ) );
    EXPECT_CALL( system_off_, init() ).WillOnce( Return( fw::Result<>{} ) );
    EXPECT_TRUE( motion_.init() );
}

TEST_F( MotionTest, InitFailsWithoutTheSensor )
{
    EXPECT_CALL( sensor_, init() ).WillOnce( Return( std::unexpected( std::errc::no_such_device ) ) );
    EXPECT_EQ( motion_.init(), std::unexpected( std::errc::no_such_device ) );
}

TEST_F( MotionTest, MovedComesFromTheSensor )
{
    EXPECT_CALL( sensor_, moved() ).WillOnce( Return( true ) ).WillOnce( Return( false ) );
    EXPECT_TRUE( motion_.moved() );
    EXPECT_FALSE( motion_.moved() );
}

TEST_F( MotionTest, SleepPowersOffAndReportsAFailure )
{
    EXPECT_CALL( system_off_, power_off() ).WillOnce( Return( std::unexpected( std::errc::not_supported ) ) );
    EXPECT_EQ( motion_.sleep_until_moved(), std::unexpected( std::errc::not_supported ) );
}

} // namespace
