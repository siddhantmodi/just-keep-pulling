/**
 * @file temperature_emul.h
 * @brief Test control API of the emulated temperature sensor (jkp,temperature-emul).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef JKP_DRIVERS_TEMPERATURE_EMUL_H_
#define JKP_DRIVERS_TEMPERATURE_EMUL_H_

#include <zephyr/device.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** @brief Set the temperature returned by the next sample fetches. */
void temperature_emul_set( const struct device *dev, int32_t millicelsius );

/** @brief Make sample fetches fail with @p error (a negative errno); 0 restores normal operation. */
void temperature_emul_set_fetch_error( const struct device *dev, int error );

#ifdef __cplusplus
}
#endif

#endif /* JKP_DRIVERS_TEMPERATURE_EMUL_H_ */
