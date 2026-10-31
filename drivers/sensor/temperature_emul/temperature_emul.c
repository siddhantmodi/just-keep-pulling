/**
 * @file temperature_emul.c
 * @brief Emulated temperature sensor for native_sim (jkp,temperature-emul).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT jkp_temperature_emul

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/spinlock.h>

#include <jkp/drivers/temperature_emul.h>

struct temperature_emul_config {
    int32_t initial_millicelsius;
};

struct temperature_emul_data {
    struct k_spinlock lock;
    int32_t millicelsius;
    int32_t sample;
    int fetch_error;
};

static int temperature_emul_sample_fetch( const struct device *dev, enum sensor_channel chan )
{
    struct temperature_emul_data *data = dev->data;
    int ret = 0;

    if( chan != SENSOR_CHAN_ALL && chan != SENSOR_CHAN_DIE_TEMP ) {
        return -ENOTSUP;
    }

    K_SPINLOCK( &data->lock )
    {
        ret = data->fetch_error;
        if( ret == 0 ) {
            data->sample = data->millicelsius;
        }
    }

    return ret;
}

static int temperature_emul_channel_get( const struct device *dev, enum sensor_channel chan, struct sensor_value *val )
{
    struct temperature_emul_data *data = dev->data;
    int32_t sample = 0;

    if( chan != SENSOR_CHAN_DIE_TEMP ) {
        return -ENOTSUP;
    }

    K_SPINLOCK( &data->lock )
    {
        sample = data->sample;
    }

    return sensor_value_from_milli( val, sample );
}

static DEVICE_API( sensor, temperature_emul_api ) = {
    .sample_fetch = temperature_emul_sample_fetch,
    .channel_get = temperature_emul_channel_get,
};

void temperature_emul_set( const struct device *dev, int32_t millicelsius )
{
    struct temperature_emul_data *data = dev->data;

    K_SPINLOCK( &data->lock )
    {
        data->millicelsius = millicelsius;
    }
}

void temperature_emul_set_fetch_error( const struct device *dev, int error )
{
    struct temperature_emul_data *data = dev->data;

    K_SPINLOCK( &data->lock )
    {
        data->fetch_error = error;
    }
}

static int temperature_emul_init( const struct device *dev )
{
    const struct temperature_emul_config *config = dev->config;
    struct temperature_emul_data *data = dev->data;

    data->millicelsius = config->initial_millicelsius;
    data->sample = config->initial_millicelsius;
    data->fetch_error = 0;

    return 0;
}

#define TEMPERATURE_EMUL_DEFINE( inst )                                                                                                           \
    static struct temperature_emul_data temperature_emul_data_##inst;                                                                             \
    static const struct temperature_emul_config temperature_emul_config_##inst = {                                                                \
        .initial_millicelsius = DT_INST_PROP( inst, millicelsius ),                                                                               \
    };                                                                                                                                            \
    SENSOR_DEVICE_DT_INST_DEFINE( inst, temperature_emul_init, NULL, &temperature_emul_data_##inst, &temperature_emul_config_##inst, POST_KERNEL, \
                                  CONFIG_SENSOR_INIT_PRIORITY, &temperature_emul_api );

DT_INST_FOREACH_STATUS_OKAY( TEMPERATURE_EMUL_DEFINE )
