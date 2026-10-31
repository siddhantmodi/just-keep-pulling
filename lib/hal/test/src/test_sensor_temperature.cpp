/**
 * @file test_sensor_temperature.cpp
 * @brief hal::zephyr::SensorTemperature against the emulated temperature sensor (jkp,temperature-emul).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

#include <cerrno>
#include <gtest/gtest.h>
#include <hal/zephyr/sensor_temperature.hpp>
#include <jkp/drivers/temperature_emul.h>
#include <system_error>

namespace
{

const device *const emul = DEVICE_DT_GET( DT_NODELABEL( temp_emul ) );
constexpr std::int32_t initial_millicelsius = DT_PROP( DT_NODELABEL( temp_emul ), millicelsius );

class SensorTemperatureTest : public ::testing::Test
{
protected:
    void TearDown() override
    {
        temperature_emul_set( emul, initial_millicelsius );
        temperature_emul_set_fetch_error( emul, 0 );
    }

    hal::zephyr::SensorTemperature sensor_{ emul, SENSOR_CHAN_DIE_TEMP };
};

TEST_F( SensorTemperatureTest, InitSucceedsForReadyDevice )
{
    EXPECT_TRUE( sensor_.init() );
}

TEST( SensorTemperatureNoDeviceTest, InitFailsWithoutDevice )
{
    hal::zephyr::SensorTemperature sensor{ nullptr, SENSOR_CHAN_DIE_TEMP };

    auto ret = sensor.init();

    ASSERT_FALSE( ret );
    EXPECT_EQ( ret.error(), std::errc::no_such_device );
}

TEST_F( SensorTemperatureTest, ReadReturnsDevicetreeValue )
{
    auto millicelsius = sensor_.read_millicelsius();

    ASSERT_TRUE( millicelsius );
    EXPECT_EQ( *millicelsius, initial_millicelsius );
}

TEST_F( SensorTemperatureTest, ReadFollowsSensorValue )
{
    temperature_emul_set( emul, -5250 );

    auto millicelsius = sensor_.read_millicelsius();

    ASSERT_TRUE( millicelsius );
    EXPECT_EQ( *millicelsius, -5250 );
}

TEST_F( SensorTemperatureTest, ReadPropagatesFetchError )
{
    temperature_emul_set_fetch_error( emul, -EIO );

    auto millicelsius = sensor_.read_millicelsius();

    ASSERT_FALSE( millicelsius );
    EXPECT_EQ( millicelsius.error(), std::errc::io_error );
}

} // namespace
