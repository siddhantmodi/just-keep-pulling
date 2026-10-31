/**
 * @file motion_svc.cpp
 * @brief The app's IMU, bound by devicetree alias: its motion trigger, and System OFF woken by its interrupt pin.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "motion_svc.hpp"

#include "motion.hpp"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

#include <hal/zephyr/gpio_wake_system_off.hpp>
#include <hal/zephyr/sensor_motion.hpp>

namespace
{

constinit hal::zephyr::SensorMotion sensor{ DEVICE_DT_GET( DT_ALIAS( motion_sensor ) ), CONFIG_APP_MOTION_SVC_THRESHOLD_MILLI_G };
constinit hal::zephyr::GpioWakeSystemOff system_off{ GPIO_DT_SPEC_GET( DT_ALIAS( motion_sensor ), irq_gpios ) };
constinit motion_svc::Motion instance{ sensor, system_off };

} // namespace

namespace motion_svc
{

IMotion &motion()
{
    return instance;
}

} // namespace motion_svc
