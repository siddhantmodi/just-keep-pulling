/**
 * @file led_svc.cpp
 * @brief The app's LED: led_svc::Led driving the devicetree led0 GPIO.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "led_svc.hpp"

#include "led.hpp"

#include <zephyr/drivers/gpio.h>

#include <hal/zephyr/gpio_output.hpp>

namespace
{

constinit hal::zephyr::GpioOutput led_pin{ GPIO_DT_SPEC_GET( DT_ALIAS( led0 ), gpios ) };
constinit led_svc::Led instance{ led_pin };

} // namespace

led_svc::ILed &led_svc::led()
{
    return instance;
}
