/**
 * @file npm1300_emul.h
 * @brief Test control API of the nPM1300 emulator (nordic,npm1300 on an I2C emulator bus).
 *
 * The emulator keeps the PMIC's registers. Starting an ADC measurement fills
 * the result registers from the battery voltage set here; the charger and
 * VBUS status registers follow the charging and USB state set here.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef JKP_DRIVERS_NPM1300_EMUL_H_
#define JKP_DRIVERS_NPM1300_EMUL_H_

#include <zephyr/drivers/emul.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** @brief The battery voltage the next measurement returns, to the ADC's 4.9 mV resolution (default 3750 mV). */
void npm1300_emul_set_battery_mv( const struct emul *target, uint32_t millivolts );

/** @brief Whether the charger reports charging (constant current) or just a battery detected. */
void npm1300_emul_set_charging( const struct emul *target, bool charging );

/** @brief Whether USB (VBUS) power is present. */
void npm1300_emul_set_vbus( const struct emul *target, bool present );

/** @brief Whether ship mode was requested since the emulator started (the real PMIC would power off). */
bool npm1300_emul_in_ship_mode( const struct emul *target );

/** @brief A register as last written by the driver, or set by the emulator. */
uint8_t npm1300_emul_get_register( const struct emul *target, uint8_t base, uint8_t offset );

#ifdef __cplusplus
}
#endif

#endif /* JKP_DRIVERS_NPM1300_EMUL_H_ */
