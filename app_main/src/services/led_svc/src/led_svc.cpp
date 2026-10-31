/**
 * @file led_svc.cpp
 * @brief The app's status LED: three GPIO outputs, bound by devicetree alias.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "led_svc.hpp"

#include "led.hpp"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

#include <hal/zephyr/gpio_output.hpp>

namespace
{

constinit hal::zephyr::GpioOutput red{ GPIO_DT_SPEC_GET( DT_ALIAS( status_led_red ), gpios ) };
constinit hal::zephyr::GpioOutput green{ GPIO_DT_SPEC_GET( DT_ALIAS( status_led_green ), gpios ) };
constinit hal::zephyr::GpioOutput blue{ GPIO_DT_SPEC_GET( DT_ALIAS( status_led_blue ), gpios ) };
constinit led_svc::Led instance{ red, green, blue };

} // namespace

namespace led_svc
{

ILed &led()
{
    return instance;
}

} // namespace led_svc
