/**
 * @file jkp_svc.cpp
 * @brief The JustKeepPulling GATT service: calibration, the sample rate and the
 *        device ID, for what the Progressor protocol has no command for.
 *
 * Any GATT client can use it (e.g. nRF Connect); Tindeq's app ignores it.
 * Writes are checked here (jkp_protocol.hpp), answered with an ATT error if
 * malformed, and published on channels::config_command for the measurement
 * manager, which owns the load cell. The calibration characteristic reads,
 * and notifies, channels::calibration_state.
 *
 * Callbacks run in Bluetooth threads; notifications are sent from the system
 * work queue.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "channels.hpp"
#include "identity_svc.hpp"
#include "jkp_protocol.hpp"
#include "load_cell_svc.hpp"

#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <cstdint>
#include <span>
#include <system_error>
#include <utility>

LOG_MODULE_REGISTER( jkp_svc, CONFIG_APP_JKP_SVC_LOG_LEVEL );

namespace
{

// a6bdXXXX-a614-4d5e-b412-e86a44627954
const bt_uuid_128 service_uuid = BT_UUID_INIT_128( BT_UUID_128_ENCODE( 0xa6bd0001, 0xa614, 0x4d5e, 0xb412, 0xe86a44627954 ) );
const bt_uuid_128 calibration_uuid = BT_UUID_INIT_128( BT_UUID_128_ENCODE( 0xa6bd0002, 0xa614, 0x4d5e, 0xb412, 0xe86a44627954 ) );
const bt_uuid_128 add_calibration_point_uuid = BT_UUID_INIT_128( BT_UUID_128_ENCODE( 0xa6bd0003, 0xa614, 0x4d5e, 0xb412, 0xe86a44627954 ) );
const bt_uuid_128 reset_calibration_uuid = BT_UUID_INIT_128( BT_UUID_128_ENCODE( 0xa6bd0004, 0xa614, 0x4d5e, 0xb412, 0xe86a44627954 ) );
const bt_uuid_128 sample_rate_uuid = BT_UUID_INIT_128( BT_UUID_128_ENCODE( 0xa6bd0005, 0xa614, 0x4d5e, 0xb412, 0xe86a44627954 ) );
const bt_uuid_128 device_id_uuid = BT_UUID_INIT_128( BT_UUID_128_ENCODE( 0xa6bd0006, 0xa614, 0x4d5e, 0xb412, 0xe86a44627954 ) );

/** @brief How long a read waits for a publisher to release the calibration channel. */
constexpr k_timeout_t channel_timeout = K_MSEC( 10 );

/** @brief The ATT error for a write that jkp_protocol.hpp rejected. */
ssize_t write_error( std::errc error )
{
    return BT_GATT_ERR( error == std::errc::message_size ? BT_ATT_ERR_INVALID_ATTRIBUTE_LEN : BT_ATT_ERR_VALUE_NOT_ALLOWED );
}

/** @brief Hand a checked request to the measurement manager. */
ssize_t publish( const channels::ConfigCommand &command, std::uint16_t len )
{
    if( auto ret = channels::config_command.publish( command ); !ret ) {
        LOG_ERR( "configuration publish failed: %d", fw::err::to_zephyr( ret.error() ) );
        return BT_GATT_ERR( BT_ATT_ERR_UNLIKELY );
    }
    return len;
}

fw::Result<std::array<std::uint8_t, jkp_svc::calibration_size>> encoded_calibration()
{
    auto state = channels::calibration_state.read( channel_timeout );
    if( !state ) {
        return std::unexpected( state.error() );
    }
    return jkp_svc::encode_calibration( std::to_underlying( state->status ), state->points, state->factor_g_per_code );
}

ssize_t read_calibration( bt_conn *conn, const bt_gatt_attr *attr, void *buf, std::uint16_t len, std::uint16_t offset )
{
    auto value = encoded_calibration();
    if( !value ) {
        return BT_GATT_ERR( BT_ATT_ERR_UNLIKELY );
    }
    return bt_gatt_attr_read( conn, attr, buf, len, offset, value->data(), value->size() );
}

ssize_t write_add_calibration_point( bt_conn * /*conn*/, const bt_gatt_attr * /*attr*/, const void *buf, std::uint16_t len, std::uint16_t offset,
                                     std::uint8_t /*flags*/ )
{
    if( offset != 0 ) {
        return BT_GATT_ERR( BT_ATT_ERR_INVALID_OFFSET );
    }
    auto kg = jkp_svc::decode_calibration_kg( { static_cast<const std::uint8_t *>( buf ), len } );
    if( !kg ) {
        return write_error( kg.error() );
    }
    LOG_INF( "calibration point at %d g requested", static_cast<int>( *kg * 1000.0F ) );
    return publish( { .type = channels::ConfigCommand::Type::add_calibration_point, .calibration_kg = *kg, .samples_per_second = 0 }, len );
}

ssize_t write_reset_calibration( bt_conn * /*conn*/, const bt_gatt_attr * /*attr*/, const void *buf, std::uint16_t len, std::uint16_t offset,
                                 std::uint8_t /*flags*/ )
{
    if( offset != 0 ) {
        return BT_GATT_ERR( BT_ATT_ERR_INVALID_OFFSET );
    }
    if( auto ret = jkp_svc::decode_reset_calibration( { static_cast<const std::uint8_t *>( buf ), len } ); !ret ) {
        return write_error( ret.error() );
    }
    return publish( { .type = channels::ConfigCommand::Type::reset_calibration, .calibration_kg = 0.0F, .samples_per_second = 0 }, len );
}

ssize_t read_sample_rate( bt_conn *conn, const bt_gatt_attr *attr, void *buf, std::uint16_t len, std::uint16_t offset )
{
    // The configured rate is the one load-cell value safe to read from another thread.
    auto const value = jkp_svc::encode_sample_rate( load_cell_svc::load_cell().sample_rate() );
    return bt_gatt_attr_read( conn, attr, buf, len, offset, value.data(), value.size() );
}

ssize_t write_sample_rate( bt_conn * /*conn*/, const bt_gatt_attr * /*attr*/, const void *buf, std::uint16_t len, std::uint16_t offset,
                           std::uint8_t /*flags*/ )
{
    if( offset != 0 ) {
        return BT_GATT_ERR( BT_ATT_ERR_INVALID_OFFSET );
    }
    auto rate = jkp_svc::decode_sample_rate( { static_cast<const std::uint8_t *>( buf ), len } );
    if( !rate ) {
        return write_error( rate.error() );
    }
    // Applied, and saved, by the measurement manager; a measurement already running keeps its rate.
    return publish(
        { .type = channels::ConfigCommand::Type::set_sample_rate, .calibration_kg = 0.0F, .samples_per_second = std::to_underlying( *rate ) }, len );
}

ssize_t read_device_id( bt_conn *conn, const bt_gatt_attr *attr, void *buf, std::uint16_t len, std::uint16_t offset )
{
    // Most significant byte first, as displayed (and as `id get` prints it).
    const auto &id = identity_svc::identity().id();
    return bt_gatt_attr_read( conn, attr, buf, len, offset, id.data(), id.size() );
}

void on_calibration_subscription( const bt_gatt_attr * /*attr*/, std::uint16_t value )
{
    LOG_DBG( "calibration notifications %s", value == BT_GATT_CCC_NOTIFY ? "on" : "off" );
}

// Not const: the stack keeps each client's subscription in it.
bt_gatt_ccc_managed_user_data calibration_ccc = BT_GATT_CCC_MANAGED_USER_DATA_INIT( on_calibration_subscription, nullptr, nullptr );

} // namespace

// NOLINTBEGIN(modernize-avoid-c-arrays): Zephyr's GATT macros declare the attribute arrays.
BT_GATT_SERVICE_DEFINE(
    jkp_service, BT_GATT_PRIMARY_SERVICE( &service_uuid.uuid ),
    BT_GATT_CHARACTERISTIC( &calibration_uuid.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_READ, read_calibration, nullptr, nullptr ),
    BT_GATT_CCC_MANAGED( &calibration_ccc, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE ), BT_GATT_CUD( "Calibration", BT_GATT_PERM_READ ),
    BT_GATT_CHARACTERISTIC( &add_calibration_point_uuid.uuid, BT_GATT_CHRC_WRITE, BT_GATT_PERM_WRITE, nullptr, write_add_calibration_point, nullptr ),
    BT_GATT_CUD( "Add calibration point (kg)", BT_GATT_PERM_READ ),
    BT_GATT_CHARACTERISTIC( &reset_calibration_uuid.uuid, BT_GATT_CHRC_WRITE, BT_GATT_PERM_WRITE, nullptr, write_reset_calibration, nullptr ),
    BT_GATT_CUD( "Reset calibration", BT_GATT_PERM_READ ),
    BT_GATT_CHARACTERISTIC( &sample_rate_uuid.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, read_sample_rate,
                            write_sample_rate, nullptr ),
    BT_GATT_CUD( "Sample rate (SPS)", BT_GATT_PERM_READ ),
    BT_GATT_CHARACTERISTIC( &device_id_uuid.uuid, BT_GATT_CHRC_READ, BT_GATT_PERM_READ, read_device_id, nullptr, nullptr ),
    BT_GATT_CUD( "Device ID", BT_GATT_PERM_READ ) );
// NOLINTEND(modernize-avoid-c-arrays)

namespace
{

/** @brief The calibration's value attribute: after the service and the characteristic declarations. */
const bt_gatt_attr &calibration_attr = jkp_service.attrs[2];

void notify_calibration( k_work * /*work*/ )
{
    auto value = encoded_calibration();
    if( !value ) {
        LOG_ERR( "calibration not read: %d", fw::err::to_zephyr( value.error() ) );
        return;
    }
    // To every subscribed client; none is not an error.
    if( int const err = bt_gatt_notify( nullptr, &calibration_attr, value->data(), value->size() ); err != 0 && err != -ENOTCONN ) {
        LOG_DBG( "calibration not notified: %d", err );
    }
}

/** @brief zbus listener on channels::calibration_state: runs with the channel locked, so it only schedules the notification. */
void on_calibration_state( const zbus_channel *chan );

} // namespace

K_WORK_DEFINE( jkp_svc_calibration_work, notify_calibration );
ZBUS_LISTENER_DEFINE( jkp_svc_calibration_listener, on_calibration_state );
ZBUS_CHAN_ADD_OBS( calibration_state_chan, jkp_svc_calibration_listener, 0 );

namespace
{

void on_calibration_state( const zbus_channel * /*chan*/ )
{
    k_work_submit( &jkp_svc_calibration_work );
}

} // namespace
