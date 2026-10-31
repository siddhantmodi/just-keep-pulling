/**
 * @file npm1300_emul.c
 * @brief nPM1300 emulator for native_sim (nordic,npm1300 on a zephyr,i2c-emul-controller bus).
 *
 * Registers are addressed by a base and an offset byte, as on the real chip:
 * a write starts with them and may carry data, possibly in further messages;
 * a read continues from the last address written. Writes are stored, and a few have the chip's
 * effect: starting an ADC measurement fills the result registers, and the
 * ship mode task is recorded.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nordic_npm1300

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/util.h>

#include <jkp/drivers/npm1300_emul.h>

LOG_MODULE_REGISTER( npm1300_emul, CONFIG_LOG_DEFAULT_LEVEL );

#define BASE_COUNT 0x20U

/* Registers with an effect (nPM1300 product specification). */
#define VBUS_BASE           0x02U
#define VBUS_OFFSET_STATUS  0x07U
#define VBUS_STATUS_PRESENT 0x01U

#define CHGR_BASE                    0x03U
#define CHGR_OFFSET_STATUS           0x34U
#define CHGR_STATUS_BATTERY_DETECTED 0x01U
#define CHGR_STATUS_CONSTANT_CURRENT 0x08U

#define ADC_BASE             0x05U
#define ADC_OFFSET_TASK_VBAT 0x00U
#define ADC_OFFSET_TASK_DIE  0x02U
#define ADC_OFFSET_RESULTS   0x10U

#define SHIP_BASE        0x0BU
#define SHIP_OFFSET_SHIP 0x02U

/* Battery current status in the ADC results. */
#define IBAT_STAT_DISCHARGE     0x04U
#define IBAT_STAT_CHARGE_NORMAL 0x0FU

/* 10-bit ADC codes: VBAT full scale is 5 V; 512 is an NTC at 25 C; 466 is about 25 C on the die. */
#define VBAT_FULL_SCALE_MV 5000U
#define ADC_MAX            1023U
#define NTC_25C_CODE       512U
#define DIE_25C_CODE       466U

#define DEFAULT_BATTERY_MV 3750U

struct npm1300_emul_data {
    struct k_spinlock lock;
    uint8_t registers[BASE_COUNT][256];
    uint8_t base;
    uint8_t offset;
    uint32_t battery_mv;
    bool charging;
    bool vbus;
    bool ship_mode;
};

/* Lay the codes out as the chip does: 8 MSBs per channel, 2 LSBs packed together. */
static void fill_adc_results( struct npm1300_emul_data *data )
{
    uint8_t *results = &data->registers[ADC_BASE][ADC_OFFSET_RESULTS];
    uint32_t vbat = MIN( ( data->battery_mv * 1024U + VBAT_FULL_SCALE_MV / 2U ) / VBAT_FULL_SCALE_MV, ADC_MAX );

    results[0] = data->charging ? IBAT_STAT_CHARGE_NORMAL : IBAT_STAT_DISCHARGE;
    results[1] = (uint8_t)( vbat >> 2 );
    results[2] = (uint8_t)( NTC_25C_CODE >> 2 );
    results[3] = (uint8_t)( DIE_25C_CODE >> 2 );
    results[5] = (uint8_t)( ( vbat & 0x3U ) | ( ( NTC_25C_CODE & 0x3U ) << 2 ) | ( ( DIE_25C_CODE & 0x3U ) << 4 ) );
    results[8] = 0; /* battery current: none */
    results[10] = 0;
}

static void update_status( struct npm1300_emul_data *data )
{
    data->registers[CHGR_BASE][CHGR_OFFSET_STATUS] = CHGR_STATUS_BATTERY_DETECTED | ( data->charging ? CHGR_STATUS_CONSTANT_CURRENT : 0U );
    data->registers[VBUS_BASE][VBUS_OFFSET_STATUS] = data->vbus ? VBUS_STATUS_PRESENT : 0U;
}

static void write_register( struct npm1300_emul_data *data, uint8_t base, uint8_t offset, uint8_t value )
{
    if( base >= BASE_COUNT ) {
        return;
    }
    data->registers[base][offset] = value;

    if( base == ADC_BASE && offset >= ADC_OFFSET_TASK_VBAT && offset <= ADC_OFFSET_TASK_DIE && value != 0U ) {
        fill_adc_results( data );
    } else if( base == SHIP_BASE && offset == SHIP_OFFSET_SHIP && value != 0U ) {
        data->ship_mode = true;
        LOG_INF( "ship mode entered" );
    }
}

static int npm1300_emul_transfer( const struct emul *target, struct i2c_msg *msgs, int num_msgs, int addr )
{
    struct npm1300_emul_data *data = target->data;

    ARG_UNUSED( addr );

    K_SPINLOCK( &data->lock )
    {
        /*
         * Bytes written since the start (or a repeated start) of the transfer:
         * the first two are the register address, the rest are data. A write
         * message without a repeated start continues the previous one.
         */
        uint32_t written = 0;
        uint32_t read = 0;

        for( int m = 0; m < num_msgs; m++ ) {
            struct i2c_msg *msg = &msgs[m];

            if( ( msg->flags & I2C_MSG_RESTART ) != 0U ) {
                written = 0;
            }
            if( ( msg->flags & I2C_MSG_READ ) != 0U ) {
                for( uint32_t i = 0; i < msg->len; i++, read++ ) {
                    msg->buf[i] = data->base < BASE_COUNT ? data->registers[data->base][(uint8_t)( data->offset + read )] : 0U;
                }
                continue;
            }
            for( uint32_t i = 0; i < msg->len; i++, written++ ) {
                if( written == 0U ) {
                    data->base = msg->buf[i];
                } else if( written == 1U ) {
                    data->offset = msg->buf[i];
                    read = 0;
                } else {
                    write_register( data, data->base, (uint8_t)( data->offset + written - 2U ), msg->buf[i] );
                }
            }
        }
    }
    return 0;
}

void npm1300_emul_set_battery_mv( const struct emul *target, uint32_t millivolts )
{
    struct npm1300_emul_data *data = target->data;

    K_SPINLOCK( &data->lock )
    {
        data->battery_mv = millivolts;
    }
}

void npm1300_emul_set_charging( const struct emul *target, bool charging )
{
    struct npm1300_emul_data *data = target->data;

    K_SPINLOCK( &data->lock )
    {
        data->charging = charging;
        update_status( data );
    }
}

void npm1300_emul_set_vbus( const struct emul *target, bool present )
{
    struct npm1300_emul_data *data = target->data;

    K_SPINLOCK( &data->lock )
    {
        data->vbus = present;
        update_status( data );
    }
}

bool npm1300_emul_in_ship_mode( const struct emul *target )
{
    struct npm1300_emul_data *data = target->data;
    bool ship_mode = false;

    K_SPINLOCK( &data->lock )
    {
        ship_mode = data->ship_mode;
    }
    return ship_mode;
}

uint8_t npm1300_emul_get_register( const struct emul *target, uint8_t base, uint8_t offset )
{
    struct npm1300_emul_data *data = target->data;
    uint8_t value = 0;

    K_SPINLOCK( &data->lock )
    {
        value = base < BASE_COUNT ? data->registers[base][offset] : 0U;
    }
    return value;
}

static int npm1300_emul_init( const struct emul *target, const struct device *parent )
{
    struct npm1300_emul_data *data = target->data;

    ARG_UNUSED( parent );

    data->battery_mv = DEFAULT_BATTERY_MV;
    update_status( data );
    return 0;
}

static const struct i2c_emul_api npm1300_emul_api = {
    .transfer = npm1300_emul_transfer,
};

#define NPM1300_EMUL_DEFINE( inst )                           \
    static struct npm1300_emul_data npm1300_emul_data_##inst; \
    EMUL_DT_INST_DEFINE( inst, npm1300_emul_init, &npm1300_emul_data_##inst, NULL, &npm1300_emul_api, NULL )

DT_INST_FOREACH_STATUS_OKAY( NPM1300_EMUL_DEFINE )
