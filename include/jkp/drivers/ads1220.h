/**
 * @file ads1220.h
 * @brief TI ADS1220 load-cell ADC driver (jkp,ads1220).
 *
 * The ADC stays powered down until started. Started, it converts continuously
 * at the requested rate; each sample is read when /DRDY signals it. A sample
 * that is not read before the next one completes is lost (the ADC keeps only
 * the latest).
 *
 * Not thread-safe: use from one thread.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef JKP_DRIVERS_ADS1220_H_
#define JKP_DRIVERS_ADS1220_H_

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** @brief One conversion. */
struct ads1220_sample {
    /** The signed 24-bit result. */
    int32_t raw;
    /** Microseconds since boot when /DRDY signalled the conversion (taken in the interrupt). */
    uint64_t ready_us;
};

/**
 * @brief Start continuous conversions. Resets the lost-sample count.
 *
 * @param samples_per_second One of the normal-mode rates: 20, 45, 90, 175, 330, 600 or 1000.
 * @retval -ENOTSUP for any other rate.
 */
int ads1220_start( const struct device *dev, uint16_t samples_per_second );

/**
 * @brief Wait for the next conversion and read it.
 *
 * @retval -EPERM if not started.
 * @retval -EAGAIN if no conversion completed within @p timeout.
 */
int ads1220_read( const struct device *dev, struct ads1220_sample *sample, k_timeout_t timeout );

/** @brief Whether @p samples_per_second is one of the rates ads1220_start() accepts. */
bool ads1220_supports_rate( uint16_t samples_per_second );

/** @brief Conversions overwritten before they were read, since the last start. */
uint32_t ads1220_lost_samples( const struct device *dev );

/** @brief Stop converting and power down (opening the low-side switch, if used). */
int ads1220_stop( const struct device *dev );

#ifdef __cplusplus
}
#endif

#endif /* JKP_DRIVERS_ADS1220_H_ */
