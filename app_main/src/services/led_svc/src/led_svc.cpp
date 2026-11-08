/**
 * @file led_svc.cpp
 * @brief The app's status LED: three Zephyr LEDs, bound by devicetree alias.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "led_svc.hpp"

#include "led.hpp"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/led.h>

namespace
{

constinit led_svc::Led instance{ LED_DT_SPEC_GET( DT_ALIAS( status_led_red ) ), LED_DT_SPEC_GET( DT_ALIAS( status_led_green ) ),
                                 LED_DT_SPEC_GET( DT_ALIAS( status_led_blue ) ) };

} // namespace

namespace led_svc
{

ILed &led()
{
    return instance;
}

} // namespace led_svc
