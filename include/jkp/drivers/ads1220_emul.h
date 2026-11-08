/**
 * @file ads1220_emul.h
 * @brief Test control API of the ADS1220 emulator (jkp,ads1220 on an SPI emulator bus).
 *
 * The emulator converts at the rate written to register 1 once started, and
 * signals each conversion on the node's /DRDY pin (an emulated GPIO). Each
 * conversion takes its value from the source, or the fixed raw value if none
 * is set.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef JKP_DRIVERS_ADS1220_EMUL_H_
#define JKP_DRIVERS_ADS1220_EMUL_H_

#include <zephyr/drivers/emul.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** @brief Supplies the value of conversion number @p index (counted from the last start). */
typedef int32_t ( *ads1220_emul_source_t )( void *context, uint32_t index );

/** @brief Make every conversion return @p raw (clamped to 24 bits). Clears any source. */
void ads1220_emul_set_raw( const struct emul *target, int32_t raw );

/** @brief Take each conversion's value from @p source; NULL returns to the fixed raw value. */
void ads1220_emul_set_source( const struct emul *target, ads1220_emul_source_t source, void *context );

/** @brief Copy the four configuration registers as last written. */
void ads1220_emul_get_registers( const struct emul *target, uint8_t registers[4] );

/** @brief Conversions completed since the last start. */
uint32_t ads1220_emul_conversions( const struct emul *target );

/** @brief Whether the emulated ADC is converting (started and not powered down). */
bool ads1220_emul_is_converting( const struct emul *target );

#ifdef __cplusplus
}
#endif

#endif /* JKP_DRIVERS_ADS1220_EMUL_H_ */
