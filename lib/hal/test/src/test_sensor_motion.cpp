/**
 * @file test_sensor_motion.cpp
 * @brief hal::zephyr::SensorMotion and GpioWakeSystemOff against Zephyr's LSM6DSL driver and emulator.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "lsm6dsl.h"
#include "lsm6dsl_emul.h"

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include <cstdint>
#include <gtest/gtest.h>
#include <hal/zephyr/gpio_wake_system_off.hpp>
#include <hal/zephyr/sensor_motion.hpp>
#include <system_error>

namespace
{

const emul *const imu = EMUL_DT_GET( DT_NODELABEL( imu ) );

std::uint8_t imu_register( std::uint8_t address )
{
    std::uint8_t value = 0;
    lsm6dsl_emul_get_reg( imu, address, &value, 1 );
    return value;
}

/** @brief The trigger handler runs on the system work queue: give it a moment. */
bool moved_within( hal::IMotionSensor &sensor, k_timeout_t timeout )
{
    k_timepoint_t const end = sys_timepoint_calc( timeout );
    while( !sys_timepoint_expired( end ) ) {
        if( sensor.moved() ) {
            return true;
        }
        k_msleep( 1 );
    }
    return false;
}

// One instance for the suite: the driver keeps a pointer to its trigger.
hal::zephyr::SensorMotion motion{ DEVICE_DT_GET( DT_NODELABEL( imu ) ), 63 };

TEST( SensorMotionTest, InitStartsTheDeferredSensorAndArmsMotion )
{
    ASSERT_TRUE( motion.init() );

    EXPECT_NE( imu_register( LSM6DSL_REG_CTRL1_XL ) & LSM6DSL_MASK_CTRL1_XL_ODR_XL, 0U ) << "accelerometer running";
    EXPECT_EQ( imu_register( LSM6DSL_REG_CTRL2_G ) & LSM6DSL_MASK_CTRL2_G_ODR_G, 0U ) << "gyroscope off";
    EXPECT_EQ( imu_register( LSM6DSL_REG_WAKE_UP_THS ) & LSM6DSL_MASK_WAKE_UP_THS_WK_THS, 2U ) << "63 mg";
    EXPECT_NE( imu_register( LSM6DSL_REG_MD1_CFG ) & LSM6DSL_MASK_MD1_CFG_INT1_WU, 0U ) << "wake-up on INT1";
    EXPECT_EQ( imu_register( LSM6DSL_REG_INT1_CTRL ), 0U ) << "nothing else on INT1";
}

TEST( SensorMotionTest, ReportsAMoveOnce )
{
    ASSERT_TRUE( motion.init() );
    EXPECT_FALSE( motion.moved() );

    lsm6dsl_emul_wake_up( imu );

    EXPECT_TRUE( moved_within( motion, K_MSEC( 100 ) ) );
    EXPECT_FALSE( motion.moved() );
}

TEST( GpioWakeSystemOffTest, IsNotSupportedOnNativeSim )
{
    hal::zephyr::GpioWakeSystemOff system_off{ GPIO_DT_SPEC_GET( DT_NODELABEL( imu ), irq_gpios ) };

    EXPECT_TRUE( system_off.init() );
    EXPECT_EQ( system_off.power_off(), std::unexpected( std::errc::not_supported ) );
}

} // namespace
