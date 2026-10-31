/**
 * @file test_npm13xx.cpp
 * @brief hal::zephyr::Npm13xxCharger and RegulatorShipMode against the nPM1300 emulator,
 *        through Zephyr's own nPM13xx drivers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>

#include <gtest/gtest.h>
#include <hal/zephyr/npm13xx_charger.hpp>
#include <hal/zephyr/regulator_ship_mode.hpp>
#include <jkp/drivers/npm1300_emul.h>

namespace
{

const emul *const pmic = EMUL_DT_GET( DT_NODELABEL( pmic ) );

class Npm13xxTest : public ::testing::Test
{
protected:
    void TearDown() override
    {
        npm1300_emul_set_battery_mv( pmic, 3750 );
        npm1300_emul_set_charging( pmic, false );
        npm1300_emul_set_vbus( pmic, false );
    }

    hal::zephyr::Npm13xxCharger charger_{ DEVICE_DT_GET( DT_NODELABEL( pmic_charger ) ) };
    hal::zephyr::RegulatorShipMode ship_mode_{ DEVICE_DT_GET( DT_NODELABEL( pmic_regulators ) ) };
};

TEST_F( Npm13xxTest, InitSucceedsForReadyDevices )
{
    EXPECT_TRUE( charger_.init() );
    EXPECT_TRUE( ship_mode_.init() );
}

TEST_F( Npm13xxTest, ReadsTheBatteryVoltage )
{
    auto status = charger_.read();
    ASSERT_TRUE( status );
    EXPECT_EQ( status->millivolts, 3750U ); // 768 codes of 5000/1024 mV: exact
    EXPECT_FALSE( status->charging );
    EXPECT_FALSE( status->external_power );
}

TEST_F( Npm13xxTest, VoltageHasTheAdcsResolution )
{
    npm1300_emul_set_battery_mv( pmic, 3300 );
    auto status = charger_.read();
    ASSERT_TRUE( status );
    EXPECT_NEAR( status->millivolts, 3300U, 5U );
}

TEST_F( Npm13xxTest, ReportsChargingAndUsbPower )
{
    npm1300_emul_set_charging( pmic, true );
    npm1300_emul_set_vbus( pmic, true );
    auto status = charger_.read();
    ASSERT_TRUE( status );
    EXPECT_TRUE( status->charging );
    EXPECT_TRUE( status->external_power );
}

TEST_F( Npm13xxTest, UsbPowerWithoutChargingIsNotCharging )
{
    npm1300_emul_set_vbus( pmic, true ); // e.g. charge complete
    auto status = charger_.read();
    ASSERT_TRUE( status );
    EXPECT_FALSE( status->charging );
    EXPECT_TRUE( status->external_power );
}

TEST_F( Npm13xxTest, ChargerIsConfiguredFromDevicetree )
{
    // Zephyr's driver enabled charging at boot (charging-enable).
    EXPECT_EQ( npm1300_emul_get_register( pmic, 0x03, 0x04 ), 1U );
}

// Last: the emulator stays in ship mode afterwards.
TEST_F( Npm13xxTest, PowerOffEntersShipMode )
{
    EXPECT_FALSE( npm1300_emul_in_ship_mode( pmic ) );
    EXPECT_TRUE( ship_mode_.power_off() );
    EXPECT_TRUE( npm1300_emul_in_ship_mode( pmic ) );
}

} // namespace
