/**
 * @file temp_svc.cpp
 * @brief The app's die temperature: temp_svc::DieTemperature reading the devicetree die-temp sensor.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "temp_svc.hpp"

#include "die_temperature.hpp"

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>

#include <hal/zephyr/sensor_temperature.hpp>

K_MUTEX_DEFINE( temp_svc_read_mutex );

namespace
{

constinit hal::zephyr::SensorTemperature die_sensor{ DEVICE_DT_GET( DT_ALIAS( die_temp ) ), SENSOR_CHAN_DIE_TEMP };
constinit temp_svc::DieTemperature instance{ die_sensor, temp_svc_read_mutex };

} // namespace

temp_svc::ITemperature &temp_svc::temperature()
{
    return instance;
}
