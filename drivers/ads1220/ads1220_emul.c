/**
 * @file ads1220_emul.c
 * @brief ADS1220 emulator for native_sim (jkp,ads1220 on a zephyr,spi-emul-controller bus).
 *
 * Decodes the commands the real chip accepts (RESET, START/SYNC, POWERDOWN,
 * RDATA, RREG, WREG), converts at the configured rate on a timer and pulses
 * the emulated /DRDY pin for each conversion.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT jkp_ads1220

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/spi_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/util.h>

#include <jkp/drivers/ads1220_emul.h>
#include <string.h>

LOG_MODULE_REGISTER( ads1220_emul, CONFIG_LOG_DEFAULT_LEVEL );

#define REGISTER_COUNT 4
#define MAX_TRANSFER   8

#define RAW_MAX ( ( 1 << 23 ) - 1 )
#define RAW_MIN ( -( 1 << 23 ) )

static const uint16_t data_rates[] = { 20, 45, 90, 175, 330, 600, 1000 };

struct ads1220_emul_cfg {
    struct gpio_dt_spec drdy;
};

struct ads1220_emul_data {
    const struct emul *target;
    struct k_spinlock lock;
    struct k_timer timer;
    uint8_t registers[REGISTER_COUNT];
    bool converting;
    int32_t raw;
    ads1220_emul_source_t source;
    void *source_context;
    uint32_t conversions;
    int32_t latched;
};

static void set_drdy( const struct emul *target, bool ready )
{
    const struct ads1220_emul_cfg *cfg = target->cfg;

    /* /DRDY is active low: low means a conversion is ready. */
    (void)gpio_emul_input_set( cfg->drdy.port, cfg->drdy.pin, ready ? 0 : 1 );
}

static void stop_converting( const struct emul *target )
{
    struct ads1220_emul_data *data = target->data;

    k_timer_stop( &data->timer );
    data->converting = false;
    set_drdy( target, false );
}

static void conversion_done( struct k_timer *timer )
{
    struct ads1220_emul_data *data = CONTAINER_OF( timer, struct ads1220_emul_data, timer );
    int32_t value = 0;

    K_SPINLOCK( &data->lock )
    {
        value = data->source != NULL ? data->source( data->source_context, data->conversions ) : data->raw;
        data->latched = CLAMP( value, RAW_MIN, RAW_MAX );
        data->conversions++;
    }

    /* The real chip raises /DRDY just before new data and lowers it when the data is ready. */
    set_drdy( data->target, false );
    set_drdy( data->target, true );
}

static void start_converting( const struct emul *target )
{
    struct ads1220_emul_data *data = target->data;
    uint8_t rate_index = data->registers[1] >> 5;
    k_timeout_t period;

    if( rate_index >= ARRAY_SIZE( data_rates ) ) {
        LOG_ERR( "data rate %u not emulated", rate_index );
        return;
    }
    period = K_USEC( USEC_PER_SEC / data_rates[rate_index] );

    data->conversions = 0;
    data->converting = true;
    set_drdy( target, false );
    /* Continuous mode converts until stopped; single-shot converts once. */
    k_timer_start( &data->timer, period, ( data->registers[1] & BIT( 2 ) ) ? period : K_NO_WAIT );
}

static int ads1220_emul_io( const struct emul *target, const struct spi_config *config, const struct spi_buf_set *tx_bufs,
                            const struct spi_buf_set *rx_bufs )
{
    struct ads1220_emul_data *data = target->data;
    uint8_t tx[MAX_TRANSFER] = { 0 };
    uint8_t rx[MAX_TRANSFER] = { 0 };
    size_t len = 0;
    uint8_t command;
    uint8_t first;
    uint8_t count;

    ARG_UNUSED( config );

    /* Flatten the transmit buffers: the driver sends one command per transfer. */
    for( size_t i = 0; tx_bufs != NULL && i < tx_bufs->count; i++ ) {
        const struct spi_buf *buf = &tx_bufs->buffers[i];

        if( len + buf->len > sizeof( tx ) ) {
            return -EIO;
        }
        if( buf->buf != NULL ) {
            memcpy( &tx[len], buf->buf, buf->len );
        }
        len += buf->len;
    }
    if( len == 0 ) {
        return -EIO;
    }

    command = tx[0];
    first = ( command >> 2 ) & 0x3U;
    count = ( command & 0x3U ) + 1;

    if( ( command & 0xFEU ) == 0x06U ) { /* RESET */
        stop_converting( target );
        memset( data->registers, 0, sizeof( data->registers ) );
    } else if( ( command & 0xFEU ) == 0x08U ) { /* START/SYNC */
        start_converting( target );
    } else if( ( command & 0xFEU ) == 0x02U ) { /* POWERDOWN */
        stop_converting( target );
    } else if( ( command & 0xF0U ) == 0x10U ) { /* RDATA */
        uint32_t bits = 0;

        K_SPINLOCK( &data->lock )
        {
            bits = (uint32_t)data->latched;
        }
        rx[1] = (uint8_t)( bits >> 16 );
        rx[2] = (uint8_t)( bits >> 8 );
        rx[3] = (uint8_t)bits;
        set_drdy( target, false );
    } else if( ( command & 0xF0U ) == 0x20U ) { /* RREG */
        for( uint8_t i = 0; i < count && first + i < REGISTER_COUNT; i++ ) {
            rx[1 + i] = data->registers[first + i];
        }
    } else if( ( command & 0xF0U ) == 0x40U ) { /* WREG */
        for( uint8_t i = 0; i < count && first + i < REGISTER_COUNT && 1 + i < len; i++ ) {
            data->registers[first + i] = tx[1 + i];
        }
    } else {
        LOG_ERR( "unknown command 0x%02x", command );
        return -EIO;
    }

    /* Copy the response into the receive buffers. */
    len = 0;
    for( size_t i = 0; rx_bufs != NULL && i < rx_bufs->count; i++ ) {
        const struct spi_buf *buf = &rx_bufs->buffers[i];

        if( buf->buf != NULL ) {
            memcpy( buf->buf, &rx[len], MIN( buf->len, sizeof( rx ) - len ) );
        }
        len += buf->len;
    }
    return 0;
}

void ads1220_emul_set_raw( const struct emul *target, int32_t raw )
{
    struct ads1220_emul_data *data = target->data;

    K_SPINLOCK( &data->lock )
    {
        data->raw = CLAMP( raw, RAW_MIN, RAW_MAX );
        data->source = NULL;
        data->source_context = NULL;
    }
}

void ads1220_emul_set_source( const struct emul *target, ads1220_emul_source_t source, void *context )
{
    struct ads1220_emul_data *data = target->data;

    K_SPINLOCK( &data->lock )
    {
        data->source = source;
        data->source_context = context;
    }
}

void ads1220_emul_get_registers( const struct emul *target, uint8_t registers[4] )
{
    const struct ads1220_emul_data *data = target->data;

    memcpy( registers, data->registers, REGISTER_COUNT );
}

uint32_t ads1220_emul_conversions( const struct emul *target )
{
    struct ads1220_emul_data *data = target->data;
    uint32_t conversions = 0;

    K_SPINLOCK( &data->lock )
    {
        conversions = data->conversions;
    }
    return conversions;
}

bool ads1220_emul_is_converting( const struct emul *target )
{
    const struct ads1220_emul_data *data = target->data;

    return data->converting;
}

static int ads1220_emul_init( const struct emul *target, const struct device *parent )
{
    struct ads1220_emul_data *data = target->data;

    ARG_UNUSED( parent );

    data->target = target;
    k_timer_init( &data->timer, conversion_done, NULL );
    // /DRDY is set from the first command: the driver has configured the pin as an input by then.
    return 0;
}

static const struct spi_emul_api ads1220_emul_api = {
    .io = ads1220_emul_io,
};

#define ADS1220_EMUL_DEFINE( inst )                                  \
    static struct ads1220_emul_data ads1220_emul_data_##inst;        \
    static const struct ads1220_emul_cfg ads1220_emul_cfg_##inst = { \
        .drdy = GPIO_DT_SPEC_INST_GET( inst, drdy_gpios ),           \
    };                                                               \
    EMUL_DT_INST_DEFINE( inst, ads1220_emul_init, &ads1220_emul_data_##inst, &ads1220_emul_cfg_##inst, &ads1220_emul_api, NULL )

DT_INST_FOREACH_STATUS_OKAY( ADS1220_EMUL_DEFINE )
