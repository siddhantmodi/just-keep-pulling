/**
 * @file tindeq_service.cpp
 * @brief The Tindeq Progressor GATT service, as Tindeq's app expects it.
 *
 * A control point write is decoded (progressor.hpp) and published on
 * channels::progressor_command for the measurement manager, which answers
 * through ble_svc::client() on the data point. Callbacks run in Bluetooth
 * threads.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tindeq_service.hpp"

#include "channels.hpp"
#include "progressor.hpp"

#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>

#include <cstdint>
#include <span>
#include <system_error>

LOG_MODULE_DECLARE( ble_svc, CONFIG_APP_BLE_SVC_LOG_LEVEL );

namespace ble_svc
{

const bt_uuid_128 tindeq_service_uuid = BT_UUID_INIT_128( BT_UUID_128_ENCODE( 0x7e4e1701, 0x1ea6, 0x40c9, 0x9dcc, 0x13d34ffead57 ) );

} // namespace ble_svc

namespace
{

const bt_uuid_128 data_point_uuid = BT_UUID_INIT_128( BT_UUID_128_ENCODE( 0x7e4e1702, 0x1ea6, 0x40c9, 0x9dcc, 0x13d34ffead57 ) );
const bt_uuid_128 control_point_uuid = BT_UUID_INIT_128( BT_UUID_128_ENCODE( 0x7e4e1703, 0x1ea6, 0x40c9, 0x9dcc, 0x13d34ffead57 ) );

ssize_t write_control_point( bt_conn * /*conn*/, const bt_gatt_attr * /*attr*/, const void *buf, std::uint16_t len, std::uint16_t offset,
                             std::uint8_t /*flags*/ )
{
    if( offset != 0 ) {
        return BT_GATT_ERR( BT_ATT_ERR_INVALID_OFFSET );
    }
    std::span const bytes{ static_cast<const std::uint8_t *>( buf ), len };

    // Malformed and unknown commands are accepted and ignored, as Crimpdeq does:
    // the protocol has no error response, and most writes come without one.
    auto command = progressor::decode_command( bytes );
    if( !command ) {
        if( command.error() == std::errc::not_supported ) {
            LOG_WRN( "unsupported opcode 0x%02x ignored", bytes[0] );
        } else {
            LOG_WRN( "malformed command (%u bytes) ignored", len );
        }
        return len;
    }
    LOG_DBG( "command 0x%02x", static_cast<unsigned>( command->opcode ) );
    if( auto ret = channels::progressor_command.publish( *command ); !ret ) {
        LOG_ERR( "command publish failed: %d", fw::err::to_zephyr( ret.error() ) );
        return BT_GATT_ERR( BT_ATT_ERR_UNLIKELY );
    }
    return len;
}

void on_data_point_subscription( const bt_gatt_attr * /*attr*/, std::uint16_t value )
{
    LOG_INF( "data point notifications %s", value == BT_GATT_CCC_NOTIFY ? "on" : "off" );
}

// Not const: the stack keeps each client's subscription in it.
bt_gatt_ccc_managed_user_data data_point_ccc = BT_GATT_CCC_MANAGED_USER_DATA_INIT( on_data_point_subscription, nullptr, nullptr );

} // namespace

// NOLINTBEGIN(modernize-avoid-c-arrays): Zephyr's GATT macros declare the attribute arrays.
BT_GATT_SERVICE_DEFINE( progressor_service, BT_GATT_PRIMARY_SERVICE( &ble_svc::tindeq_service_uuid.uuid ),
                        BT_GATT_CHARACTERISTIC( &data_point_uuid.uuid, BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_NONE, nullptr, nullptr, nullptr ),
                        BT_GATT_CCC_MANAGED( &data_point_ccc, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE ),
                        BT_GATT_CHARACTERISTIC( &control_point_uuid.uuid, BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP, BT_GATT_PERM_WRITE,
                                                nullptr, write_control_point, nullptr ) );
// NOLINTEND(modernize-avoid-c-arrays)

namespace ble_svc
{

const bt_gatt_attr &tindeq_data_point() noexcept
{
    // After the service and the characteristic declarations.
    return progressor_service.attrs[2];
}

} // namespace ble_svc
