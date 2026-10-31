/**
 * @file ble_svc.cpp
 * @brief BLE adapter: GATT table, characteristic callbacks and advertising.
 *
 * Callbacks run in the Bluetooth RX thread. They decode/encode with
 * gatt_codec.hpp and otherwise only touch thread-safe APIs: LED writes are
 * published to zbus for the app manager, LED reads come from the led_state
 * channel, and temperature reads call temp_svc directly (a GATT read must be
 * answered synchronously, and temp_svc serializes reads).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ble_svc.hpp"

#include "channels.hpp"
#include "gatt_codec.hpp"
#include "temp_svc.hpp"

#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <array>
#include <cstdint>
#include <span>

LOG_MODULE_REGISTER( ble_svc, CONFIG_APP_BLE_SVC_LOG_LEVEL );

static_assert( static_cast<std::uint8_t>( ble_svc::AttError::invalid_offset ) == BT_ATT_ERR_INVALID_OFFSET );
static_assert( static_cast<std::uint8_t>( ble_svc::AttError::invalid_attribute_length ) == BT_ATT_ERR_INVALID_ATTRIBUTE_LEN );
static_assert( static_cast<std::uint8_t>( ble_svc::AttError::unlikely ) == BT_ATT_ERR_UNLIKELY );
static_assert( static_cast<std::uint8_t>( ble_svc::AttError::value_not_allowed ) == BT_ATT_ERR_VALUE_NOT_ALLOWED );

namespace
{

const bt_uuid_128 service_uuid = BT_UUID_INIT_128( BT_UUID_128_ENCODE( 0x9bb40001, 0xb8c3, 0x4eba, 0xa5ac, 0x7924595b107a ) );
const bt_uuid_128 led_uuid = BT_UUID_INIT_128( BT_UUID_128_ENCODE( 0x9bb40002, 0xb8c3, 0x4eba, 0xa5ac, 0x7924595b107a ) );
const bt_uuid_128 temperature_uuid = BT_UUID_INIT_128( BT_UUID_128_ENCODE( 0x9bb40003, 0xb8c3, 0x4eba, 0xa5ac, 0x7924595b107a ) );

// Characteristic Presentation Format, so generic apps (e.g. nRF Connect) show the temperature in degrees Celsius.
const bt_gatt_cpf temperature_format = {
    .format = 0x0E,      // sint16
    .exponent = -2,      // value x 10^-2
    .unit = 0x272F,      // degrees Celsius
    .name_space = 0x01,  // Bluetooth SIG
    .description = 0x00, // unknown
};

ssize_t gatt_error( ble_svc::AttError error )
{
    return BT_GATT_ERR( static_cast<std::uint8_t>( error ) );
}

ssize_t read_led( bt_conn *conn, const bt_gatt_attr *attr, void *buf, std::uint16_t len, std::uint16_t offset )
{
    auto state = channels::led_state.read( K_MSEC( 20 ) );
    if( !state ) {
        return gatt_error( ble_svc::AttError::unlikely );
    }
    auto const value = ble_svc::encode_led_state( state->on );
    return bt_gatt_attr_read( conn, attr, buf, len, offset, value.data(), value.size() );
}

ssize_t write_led( bt_conn * /*conn*/, const bt_gatt_attr * /*attr*/, const void *buf, std::uint16_t len, std::uint16_t offset,
                   std::uint8_t /*flags*/ )
{
    auto on = ble_svc::decode_led_write( std::span{ static_cast<const std::uint8_t *>( buf ), len }, offset );
    if( !on ) {
        return gatt_error( on.error() );
    }
    if( auto ret = channels::led_request.publish( { .on = *on } ); !ret ) {
        LOG_ERR( "LED request publish failed: %d", fw::err::to_zephyr( ret.error() ) );
        return gatt_error( ble_svc::AttError::unlikely );
    }
    return len;
}

ssize_t read_temperature( bt_conn *conn, const bt_gatt_attr *attr, void *buf, std::uint16_t len, std::uint16_t offset )
{
    auto centicelsius = temp_svc::temperature().read_centicelsius();
    if( !centicelsius ) {
        return gatt_error( ble_svc::AttError::unlikely );
    }
    auto const value = ble_svc::encode_temperature( *centicelsius );
    return bt_gatt_attr_read( conn, attr, buf, len, offset, value.data(), value.size() );
}

constexpr bt_le_adv_param adv_param = BT_LE_ADV_PARAM_INIT( BT_LE_ADV_OPT_CONN, BT_GAP_ADV_FAST_INT_MIN_1, BT_GAP_ADV_FAST_INT_MAX_1, nullptr );

constexpr std::array<std::uint8_t, 1> adv_flags{ BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR };

// Name in the advertising data; the 128-bit service UUID does not fit alongside it, so it goes in the scan response.
const std::array<bt_data, 2> adv_data{ {
    BT_DATA( BT_DATA_FLAGS, adv_flags.data(), adv_flags.size() ),
    BT_DATA( BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof( CONFIG_BT_DEVICE_NAME ) - 1 ),
} };
const std::array<bt_data, 1> scan_data{ {
    BT_DATA( BT_DATA_UUID128_ALL, service_uuid.val, sizeof( service_uuid.val ) ),
} };

fw::Result<> start_advertising()
{
    return fw::err::check( bt_le_adv_start( &adv_param, adv_data.data(), adv_data.size(), scan_data.data(), scan_data.size() ) );
}

void restart_advertising( k_work * /*work*/ )
{
    // EALREADY (connection_already_in_progress) means advertising is already running.
    if( auto ret = start_advertising(); !ret && ret.error() != std::errc::connection_already_in_progress ) {
        LOG_ERR( "advertising restart failed: %d", fw::err::to_zephyr( ret.error() ) );
    }
}

void on_connected( bt_conn * /*conn*/, std::uint8_t err )
{
    if( err != 0 ) {
        LOG_WRN( "connection failed: 0x%02x", err );
        return;
    }
    LOG_INF( "connected" );
}

void on_disconnected( bt_conn * /*conn*/, std::uint8_t reason )
{
    LOG_INF( "disconnected: 0x%02x", reason );
}

} // namespace

// NOLINTBEGIN(modernize-avoid-c-arrays): Zephyr's GATT macros declare the attribute arrays.
BT_GATT_SERVICE_DEFINE( jkp_service, BT_GATT_PRIMARY_SERVICE( &service_uuid.uuid ),
                        BT_GATT_CHARACTERISTIC( &led_uuid.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                                                read_led, write_led, nullptr ),
                        BT_GATT_CUD( "LED", BT_GATT_PERM_READ ),
                        BT_GATT_CHARACTERISTIC( &temperature_uuid.uuid, BT_GATT_CHRC_READ, BT_GATT_PERM_READ, read_temperature, nullptr, nullptr ),
                        BT_GATT_CPF( &temperature_format ), BT_GATT_CUD( "Die temperature", BT_GATT_PERM_READ ) );
// NOLINTEND(modernize-avoid-c-arrays)

// The connection object is only free for a new connection once recycled; restart advertising from a work item then.
K_WORK_DEFINE( ble_svc_adv_work, restart_advertising );

BT_CONN_CB_DEFINE( ble_svc_conn_callbacks ) = {
    .connected = on_connected,
    .disconnected = on_disconnected,
    .recycled = [] { k_work_submit( &ble_svc_adv_work ); },
};

namespace ble_svc
{

fw::Result<> init()
{
    if( auto ret = fw::err::check( bt_enable( nullptr ) ); !ret ) {
        LOG_ERR( "bt_enable failed: %d", fw::err::to_zephyr( ret.error() ) );
        return ret;
    }
    if( auto ret = start_advertising(); !ret ) {
        LOG_ERR( "advertising start failed: %d", fw::err::to_zephyr( ret.error() ) );
        return ret;
    }
    LOG_INF( "advertising as \"%s\"", CONFIG_BT_DEVICE_NAME );
    return {};
}

} // namespace ble_svc
