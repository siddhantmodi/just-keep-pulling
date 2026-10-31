/**
 * @file ads1220.c
 * @brief TI ADS1220 load-cell ADC driver (jkp,ads1220): continuous conversions, read on /DRDY.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT jkp_ads1220

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <jkp/drivers/ads1220.h>
#include <string.h>

LOG_MODULE_REGISTER( ads1220, CONFIG_LOG_DEFAULT_LEVEL );

#define CMD_POWERDOWN 0x02U
#define CMD_RESET     0x06U
#define CMD_START     0x08U
#define CMD_RDATA     0x10U
#define CMD_RREG_ALL  0x23U /* RREG from register 0, 4 registers */
#define CMD_WREG_ALL  0x43U /* WREG from register 0, 4 registers */

#define REGISTER_COUNT 4

#define REG1_CONTINUOUS BIT( 2 )
#define REG2_VREF_REFP0 ( 0x1U << 6 ) /* external reference on REFP0/REFN0: ratiometric */
#define REG2_PSW        BIT( 3 )

/* 50 us reset time plus 32 periods of the 4.096 MHz oscillator. */
#define RESET_TIME_US 60

/* Normal-mode data rates, indexed by the DR field of register 1. */
static const uint16_t data_rates[] = { 20, 45, 90, 175, 330, 600, 1000 };

struct ads1220_config {
    struct spi_dt_spec bus;
    struct gpio_dt_spec drdy;
    uint8_t mux;
    uint8_t gain_bits;
    bool low_side_switch;
};

struct ads1220_data {
    struct gpio_callback drdy_cb;
    struct k_sem drdy_sem;
    bool started;
};

static int transfer( const struct device *dev, const uint8_t *tx, uint8_t *rx, size_t len )
{
    const struct ads1220_config *config = dev->config;
    const struct spi_buf tx_buf = { .buf = (void *)tx, .len = len };
    const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
    const struct spi_buf rx_buf = { .buf = rx, .len = len };
    const struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };

    return spi_transceive_dt( &config->bus, &tx_set, rx != NULL ? &rx_set : NULL );
}

static int send_command( const struct device *dev, uint8_t command )
{
    return transfer( dev, &command, NULL, 1 );
}

static void build_registers( const struct ads1220_config *config, uint8_t data_rate_bits, uint8_t regs[REGISTER_COUNT] )
{
    regs[0] = (uint8_t)( ( config->mux << 4 ) | ( config->gain_bits << 1 ) ); /* PGA enabled */
    regs[1] = (uint8_t)( ( data_rate_bits << 5 ) | REG1_CONTINUOUS );         /* normal mode */
    regs[2] = REG2_VREF_REFP0 | ( config->low_side_switch ? REG2_PSW : 0U );
    regs[3] = 0; /* /DRDY on its own pin only */
}

/* Write all registers and read them back, so a wiring fault shows up as an error. */
static int write_registers( const struct device *dev, const uint8_t regs[REGISTER_COUNT] )
{
    uint8_t tx[1 + REGISTER_COUNT] = { CMD_WREG_ALL };
    uint8_t rx[1 + REGISTER_COUNT];
    int ret;

    memcpy( &tx[1], regs, REGISTER_COUNT );
    ret = transfer( dev, tx, NULL, sizeof( tx ) );
    if( ret < 0 ) {
        return ret;
    }

    memset( tx, 0, sizeof( tx ) );
    tx[0] = CMD_RREG_ALL;
    ret = transfer( dev, tx, rx, sizeof( tx ) );
    if( ret < 0 ) {
        return ret;
    }
    if( memcmp( &rx[1], regs, REGISTER_COUNT ) != 0 ) {
        LOG_ERR( "register readback mismatch: %02x %02x %02x %02x", rx[1], rx[2], rx[3], rx[4] );
        return -EIO;
    }
    return 0;
}

static int data_rate_bits( uint16_t samples_per_second )
{
    for( size_t i = 0; i < ARRAY_SIZE( data_rates ); i++ ) {
        if( data_rates[i] == samples_per_second ) {
            return (int)i;
        }
    }
    return -ENOTSUP;
}

int ads1220_start( const struct device *dev, uint16_t samples_per_second )
{
    const struct ads1220_config *config = dev->config;
    struct ads1220_data *data = dev->data;
    uint8_t regs[REGISTER_COUNT];
    int bits = data_rate_bits( samples_per_second );
    int ret;

    if( bits < 0 ) {
        return bits;
    }

    build_registers( config, (uint8_t)bits, regs );
    ret = write_registers( dev, regs );
    if( ret < 0 ) {
        return ret;
    }

    k_sem_reset( &data->drdy_sem );
    ret = send_command( dev, CMD_START );
    if( ret < 0 ) {
        return ret;
    }
    data->started = true;
    return 0;
}

int ads1220_read( const struct device *dev, int32_t *raw, k_timeout_t timeout )
{
    struct ads1220_data *data = dev->data;
    uint8_t tx[4] = { CMD_RDATA };
    uint8_t rx[4];
    int ret;

    if( !data->started ) {
        return -EPERM;
    }
    ret = k_sem_take( &data->drdy_sem, timeout );
    if( ret < 0 ) {
        return -EAGAIN;
    }
    ret = transfer( dev, tx, rx, sizeof( tx ) );
    if( ret < 0 ) {
        return ret;
    }

    /* 24-bit two's complement, most significant byte first. */
    *raw = sign_extend( ( (uint32_t)rx[1] << 16 ) | ( (uint32_t)rx[2] << 8 ) | rx[3], 23 );
    return 0;
}

int ads1220_stop( const struct device *dev )
{
    struct ads1220_data *data = dev->data;

    data->started = false;
    k_sem_reset( &data->drdy_sem );
    return send_command( dev, CMD_POWERDOWN );
}

static void drdy_handler( const struct device *port, struct gpio_callback *cb, uint32_t pins )
{
    struct ads1220_data *data = CONTAINER_OF( cb, struct ads1220_data, drdy_cb );

    ARG_UNUSED( port );
    ARG_UNUSED( pins );
    k_sem_give( &data->drdy_sem );
}

static int ads1220_init( const struct device *dev )
{
    const struct ads1220_config *config = dev->config;
    struct ads1220_data *data = dev->data;
    uint8_t regs[REGISTER_COUNT];
    int ret;

    if( !spi_is_ready_dt( &config->bus ) || !gpio_is_ready_dt( &config->drdy ) ) {
        return -ENODEV;
    }

    /* Holds at most one pending conversion: the ADC only keeps the latest. */
    k_sem_init( &data->drdy_sem, 0, 1 );

    ret = gpio_pin_configure_dt( &config->drdy, GPIO_INPUT );
    if( ret < 0 ) {
        return ret;
    }
    gpio_init_callback( &data->drdy_cb, drdy_handler, BIT( config->drdy.pin ) );
    ret = gpio_add_callback_dt( &config->drdy, &data->drdy_cb );
    if( ret < 0 ) {
        return ret;
    }
    ret = gpio_pin_interrupt_configure_dt( &config->drdy, GPIO_INT_EDGE_TO_ACTIVE );
    if( ret < 0 ) {
        return ret;
    }

    ret = send_command( dev, CMD_RESET );
    if( ret < 0 ) {
        return ret;
    }
    k_busy_wait( RESET_TIME_US );

    build_registers( config, 0, regs );
    ret = write_registers( dev, regs );
    if( ret < 0 ) {
        LOG_ERR( "not responding: %d", ret );
        return ret;
    }
    return send_command( dev, CMD_POWERDOWN );
}

#define ADS1220_SPI_OPERATION ( SPI_OP_MODE_MASTER | SPI_WORD_SET( 8 ) | SPI_MODE_CPHA )

#define ADS1220_DEFINE( inst )                                                                                                                    \
    static struct ads1220_data ads1220_data_##inst;                                                                                               \
    static const struct ads1220_config ads1220_config_##inst = {                                                                                  \
        .bus = SPI_DT_SPEC_INST_GET( inst, ADS1220_SPI_OPERATION ),                                                                               \
        .drdy = GPIO_DT_SPEC_INST_GET( inst, drdy_gpios ),                                                                                        \
        .mux = DT_INST_PROP( inst, mux ),                                                                                                         \
        .gain_bits = LOG2( DT_INST_PROP( inst, gain ) ),                                                                                          \
        .low_side_switch = DT_INST_PROP( inst, low_side_power_switch ),                                                                           \
    };                                                                                                                                            \
    DEVICE_DT_INST_DEFINE( inst, ads1220_init, NULL, &ads1220_data_##inst, &ads1220_config_##inst, POST_KERNEL, CONFIG_JKP_ADS1220_INIT_PRIORITY, \
                           NULL );

DT_INST_FOREACH_STATUS_OKAY( ADS1220_DEFINE )
