/**
 * @file test_pmic.cpp
 * @brief Unit tests of pmic_svc::Pmic against a mocked charger and power switch.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pmic.hpp"

#include <expected>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <hal/mocks/mock_battery_charger.hpp>
#include <hal/mocks/mock_power_off.hpp>
#include <system_error>

namespace
{

using testing::Return;

class PmicTest : public testing::Test
{
protected:
    testing::StrictMock<hal::mocks::MockBatteryCharger> charger_;
    testing::StrictMock<hal::mocks::MockPowerOff> power_off_;
    pmic_svc::Pmic pmic_{ charger_, power_off_ };
};

TEST_F( PmicTest, InitChecksTheChargerAndThePowerSwitch )
{
    EXPECT_CALL( charger_, init() ).WillOnce( Return( fw::Result<>{} ) );
    EXPECT_CALL( power_off_, init() ).WillOnce( Return( fw::Result<>{} ) );
    EXPECT_TRUE( pmic_.init() );
}

TEST_F( PmicTest, InitFailsWhenTheChargerIsNotReady )
{
    EXPECT_CALL( charger_, init() ).WillOnce( Return( std::unexpected( std::errc::no_such_device ) ) );
    EXPECT_EQ( pmic_.init(), std::unexpected( std::errc::no_such_device ) );
}

TEST_F( PmicTest, InitFailsWhenThePowerSwitchIsNotReady )
{
    EXPECT_CALL( charger_, init() ).WillOnce( Return( fw::Result<>{} ) );
    EXPECT_CALL( power_off_, init() ).WillOnce( Return( std::unexpected( std::errc::no_such_device ) ) );
    EXPECT_EQ( pmic_.init(), std::unexpected( std::errc::no_such_device ) );
}

TEST_F( PmicTest, ReadsTheBattery )
{
    hal::BatteryStatus const status{ .millivolts = 3812, .charging = true, .external_power = true };
    EXPECT_CALL( charger_, read() ).WillOnce( Return( status ) );

    auto read = pmic_.read_battery();
    ASSERT_TRUE( read );
    EXPECT_EQ( read->millivolts, 3812U );
    EXPECT_TRUE( read->charging );
    EXPECT_TRUE( read->external_power );
}

TEST_F( PmicTest, ReportsAFailedRead )
{
    EXPECT_CALL( charger_, read() ).WillOnce( Return( std::unexpected( std::errc::io_error ) ) );
    EXPECT_EQ( pmic_.read_battery().error(), std::errc::io_error );
}

TEST_F( PmicTest, ShipModePowersOff )
{
    EXPECT_CALL( power_off_, power_off() ).WillOnce( Return( fw::Result<>{} ) );
    EXPECT_TRUE( pmic_.ship_mode() );
}

TEST_F( PmicTest, ShipModeReportsAFailure )
{
    EXPECT_CALL( power_off_, power_off() ).WillOnce( Return( std::unexpected( std::errc::io_error ) ) );
    EXPECT_EQ( pmic_.ship_mode(), std::unexpected( std::errc::io_error ) );
}

} // namespace
