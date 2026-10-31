/**
 * @file ble_svc.cpp
 * @brief BLE adapter: the Progressor GATT table, advertising, connection handling and the IClient implementation.
 *
 * Also serves the standard Device Information service (configured in Kconfig)
 * and Battery service (its level estimated from channels::battery), for
 * generic apps such as nRF Connect.
 *
 * Callbacks run in Bluetooth threads. A control point write is decoded
 * (progressor.hpp) and published to zbus for the measurement manager;
 * connection changes are published too. Notifications are sent from the
 * manager's thread through client(), without waiting for buffers, so a slow
 * link drops packets instead of stalling sampling.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ble_svc.hpp"

#include "advertising.hpp"
#include "battery_level.hpp"
#include "channels.hpp"
#include "identity_svc.hpp"
#include "progressor.hpp"

#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <expected>
#include <fw/scoped_lock.hpp>
#include <span>
#include <system_error>
#include <utility>

LOG_MODULE_REGISTER( ble_svc, CONFIG_APP_BLE_SVC_LOG_LEVEL );

static_assert( CONFIG_APP_BLE_SVC_MAX_QUEUED_NOTIFICATIONS < CONFIG_BT_ATT_TX_COUNT,
               "leave ATT buffers for responses, or sending a notification can wait for one" );

K_MUTEX_DEFINE( ble_svc_client_mutex );

namespace
{

const bt_uuid_128 service_uuid = BT_UUID_INIT_128( BT_UUID_128_ENCODE( 0x7e4e1701, 0x1ea6, 0x40c9, 0x9dcc, 0x13d34ffead57 ) );
const bt_uuid_128 data_point_uuid = BT_UUID_INIT_128( BT_UUID_128_ENCODE( 0x7e4e1702, 0x1ea6, 0x40c9, 0x9dcc, 0x13d34ffead57 ) );
const bt_uuid_128 control_point_uuid = BT_UUID_INIT_128( BT_UUID_128_ENCODE( 0x7e4e1703, 0x1ea6, 0x40c9, 0x9dcc, 0x13d34ffead57 ) );

/** @brief Fast advertising after boot or a disconnect, then slow, as Crimpdeq. Intervals in 0.625 ms units. */
constexpr std::uint16_t fast_advertising_interval = 256;  // 160 ms
constexpr std::uint16_t slow_advertising_interval = 1636; // 1022.5 ms
constexpr k_timeout_t fast_advertising_duration = K_SECONDS( 30 );

/** @brief Connection parameters requested while streaming and idle, as Crimpdeq: intervals in 1.25 ms units, timeout in 10 ms. */
constexpr bt_le_conn_param streaming_params = BT_LE_CONN_PARAM_INIT( 12, 12, 0, 400 ); // 15 ms, no latency, 4 s
constexpr bt_le_conn_param idle_params = BT_LE_CONN_PARAM_INIT( 24, 40, 4, 400 );      // 30-50 ms, latency 4, 4 s

/** @brief The ATT MTU before the client exchanges a larger one. */
constexpr std::uint16_t default_att_mtu = 23;

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
BT_GATT_SERVICE_DEFINE( progressor_service, BT_GATT_PRIMARY_SERVICE( &service_uuid.uuid ),
                        BT_GATT_CHARACTERISTIC( &data_point_uuid.uuid, BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_NONE, nullptr, nullptr, nullptr ),
                        BT_GATT_CCC_MANAGED( &data_point_ccc, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE ),
                        BT_GATT_CHARACTERISTIC( &control_point_uuid.uuid, BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP, BT_GATT_PERM_WRITE,
                                                nullptr, write_control_point, nullptr ) );
// NOLINTEND(modernize-avoid-c-arrays)

namespace
{

/** @brief The data point's value attribute: after the service and the characteristic declarations. */
const bt_gatt_attr &data_point_attr = progressor_service.attrs[2];

/**
 * @brief The connection, shared between Bluetooth callbacks and the manager's
 *        thread. Each new connection starts a new generation, so completions of
 *        an old connection's notifications don't count against the new one.
 */
class Client final : public ble_svc::IClient
{
public:
    constexpr explicit Client( k_mutex &mutex ) noexcept
        : mutex_{ mutex }
    {
    }

    [[nodiscard]] fw::Result<> send( std::span<const std::uint8_t> packet ) override;
    [[nodiscard]] std::uint16_t att_mtu() const override;
    [[nodiscard]] fw::Result<> set_streaming( bool streaming ) override;
    [[nodiscard]] fw::Result<> disconnect() override;

    void on_connected( bt_conn *conn );
    /** @brief Forget the connection. Returns whether there was one (on_connected() ran for it). */
    bool on_disconnected();
    void on_notification_sent( std::uintptr_t generation );
    [[nodiscard]] bool connected() const;
    [[nodiscard]] bool streaming() const;

private:
    /** @brief A reference to the connection (null if none) and its generation. The caller unrefs it. */
    [[nodiscard]] std::pair<bt_conn *, std::uintptr_t> acquire() const;

    k_mutex &mutex_;
    bt_conn *conn_{ nullptr };
    std::uintptr_t generation_{ 0 };
    unsigned queued_{ 0 };
    bool streaming_{ false };
};

std::pair<bt_conn *, std::uintptr_t> Client::acquire() const
{
    fw::ScopedLock const lock{ mutex_ };
    return { conn_ != nullptr ? bt_conn_ref( conn_ ) : nullptr, generation_ };
}

void notification_sent( bt_conn * /*conn*/, void *user_data );

fw::Result<> Client::send( std::span<const std::uint8_t> packet )
{
    auto [conn, generation] = acquire();
    if( conn == nullptr ) {
        return std::unexpected( std::errc::not_connected );
    }
    fw::Result<> result{};
    if( !bt_gatt_is_subscribed( conn, &data_point_attr, BT_GATT_CCC_NOTIFY ) ) {
        result = std::unexpected( std::errc::not_connected );
    } else {
        {
            fw::ScopedLock const lock{ mutex_ };
            if( generation != generation_ || queued_ >= CONFIG_APP_BLE_SVC_MAX_QUEUED_NOTIFICATIONS ) {
                result = std::unexpected( generation != generation_ ? std::errc::not_connected : std::errc::no_buffer_space );
            } else {
                ++queued_;
            }
        }
        if( result ) {
            bt_gatt_notify_params params{};
            params.attr = &data_point_attr;
            params.data = packet.data();
            params.len = static_cast<std::uint16_t>( packet.size() );
            params.func = notification_sent;
            params.user_data = reinterpret_cast<void *>( generation ); // NOLINT(performance-no-int-to-ptr): a tag, not a pointer.
            result = fw::err::check( bt_gatt_notify_cb( conn, &params ) );
            if( !result ) {
                on_notification_sent( generation ); // Not queued after all.
                if( result.error() == std::errc::not_enough_memory ) {
                    result = std::unexpected( std::errc::no_buffer_space );
                }
            }
        }
    }
    bt_conn_unref( conn );
    return result;
}

std::uint16_t Client::att_mtu() const
{
    auto [conn, generation] = acquire();
    if( conn == nullptr ) {
        return default_att_mtu;
    }
    std::uint16_t const mtu = bt_gatt_get_mtu( conn );
    bt_conn_unref( conn );
    return mtu;
}

fw::Result<> Client::set_streaming( bool streaming )
{
    {
        fw::ScopedLock const lock{ mutex_ };
        streaming_ = streaming;
    }
    auto [conn, generation] = acquire();
    if( conn == nullptr ) {
        return std::unexpected( std::errc::not_connected );
    }
    auto result = fw::err::check( bt_conn_le_param_update( conn, streaming ? &streaming_params : &idle_params ) );
    bt_conn_unref( conn );
    return result;
}

fw::Result<> Client::disconnect()
{
    auto [conn, generation] = acquire();
    if( conn == nullptr ) {
        return std::unexpected( std::errc::not_connected );
    }
    auto result = fw::err::check( bt_conn_disconnect( conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN ) );
    bt_conn_unref( conn );
    return result;
}

void Client::on_connected( bt_conn *conn )
{
    fw::ScopedLock const lock{ mutex_ };
    conn_ = bt_conn_ref( conn );
    ++generation_;
    queued_ = 0;
    streaming_ = false;
}

bool Client::on_disconnected()
{
    bt_conn *conn = nullptr;
    {
        fw::ScopedLock const lock{ mutex_ };
        conn = std::exchange( conn_, nullptr );
        ++generation_;
        queued_ = 0;
        streaming_ = false;
    }
    if( conn == nullptr ) {
        return false;
    }
    bt_conn_unref( conn );
    return true;
}

void Client::on_notification_sent( std::uintptr_t generation )
{
    fw::ScopedLock const lock{ mutex_ };
    if( generation == generation_ && queued_ > 0 ) {
        --queued_;
    }
}

bool Client::connected() const
{
    fw::ScopedLock const lock{ mutex_ };
    return conn_ != nullptr;
}

bool Client::streaming() const
{
    fw::ScopedLock const lock{ mutex_ };
    return streaming_;
}

constinit Client instance{ ble_svc_client_mutex };

void notification_sent( bt_conn * /*conn*/, void *user_data )
{
    instance.on_notification_sent( reinterpret_cast<std::uintptr_t>( user_data ) );
}

// The advertised name, set once by init().
std::array<char, ble_svc::name_size> name{};

constexpr std::array<std::uint8_t, 1> adv_flags{ BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR };

// The name in the advertising data; the 128-bit service UUID does not fit alongside it, so it goes in the scan response.
const std::array<bt_data, 2> adv_data{ {
    BT_DATA( BT_DATA_FLAGS, adv_flags.data(), adv_flags.size() ),
    BT_DATA( BT_DATA_NAME_COMPLETE, name.data(), ble_svc::name_size - 1 ),
} };
const std::array<bt_data, 1> scan_data{ {
    BT_DATA( BT_DATA_UUID128_ALL, service_uuid.val, sizeof( service_uuid.val ) ),
} };

fw::Result<> start_advertising( std::uint16_t interval )
{
    bt_le_adv_param const param = BT_LE_ADV_PARAM_INIT( BT_LE_ADV_OPT_CONN, interval, interval, nullptr );
    return fw::err::check( bt_le_adv_start( &param, adv_data.data(), adv_data.size(), scan_data.data(), scan_data.size() ) );
}

void slow_down_advertising( k_work * /*work*/ );

K_WORK_DELAYABLE_DEFINE( ble_svc_slow_adv_work, slow_down_advertising );

fw::Result<> start_fast_advertising()
{
    // EALREADY (connection_already_in_progress) means advertising is already running.
    if( auto ret = start_advertising( fast_advertising_interval ); !ret && ret.error() != std::errc::connection_already_in_progress ) {
        return ret;
    }
    k_work_reschedule( &ble_svc_slow_adv_work, fast_advertising_duration );
    return {};
}

void restart_advertising( k_work * /*work*/ )
{
    if( auto ret = start_fast_advertising(); !ret ) {
        LOG_ERR( "advertising restart failed: %d", fw::err::to_zephyr( ret.error() ) );
    }
}

void slow_down_advertising( k_work * /*work*/ )
{
    if( instance.connected() ) {
        return;
    }
    (void)bt_le_adv_stop();
    if( auto ret = start_advertising( slow_advertising_interval ); !ret ) {
        LOG_ERR( "slow advertising start failed: %d", fw::err::to_zephyr( ret.error() ) );
        return;
    }
    LOG_DBG( "advertising slowly" );
}

void on_connected( bt_conn *conn, std::uint8_t err )
{
    if( err != 0 ) {
        // Includes a central dropping the link while the stack sets it up: Zephyr
        // handles the disconnection first and reports the connection with its reason.
        LOG_INF( "connection dropped during setup: 0x%02x", err );
        return;
    }
    instance.on_connected( conn );
    k_work_cancel_delayable( &ble_svc_slow_adv_work );
    LOG_INF( "connected" );
    if( auto ret = channels::client_connection.publish( { .connected = true } ); !ret ) {
        LOG_ERR( "connection publish failed: %d", fw::err::to_zephyr( ret.error() ) );
    }
}

void on_disconnected( bt_conn * /*conn*/, std::uint8_t reason )
{
    // Also called for a connection dropped during setup, which was never announced.
    if( !instance.on_disconnected() ) {
        return;
    }
    LOG_INF( "disconnected: 0x%02x", reason );
    if( auto ret = channels::client_connection.publish( { .connected = false } ); !ret ) {
        LOG_ERR( "connection publish failed: %d", fw::err::to_zephyr( ret.error() ) );
    }
}

void on_param_updated( bt_conn * /*conn*/, std::uint16_t interval, std::uint16_t latency, std::uint16_t timeout )
{
    LOG_INF( "connection interval %u us, latency %u, timeout %u ms", interval * 1250U, latency, timeout * 10U );
    if( instance.streaming() && ( interval > streaming_params.interval_max || latency != 0 ) ) {
        LOG_WRN( "connection parameters may not keep up with high sample rates" );
    }
}

void on_att_mtu_updated( bt_conn * /*conn*/, std::uint16_t tx, std::uint16_t rx )
{
    LOG_INF( "ATT MTU tx %u, rx %u", tx, rx );
}

bt_gatt_cb gatt_callbacks = {
    .att_mtu_updated = on_att_mtu_updated,
};

// The standard Battery service, for generic apps (e.g. nRF Connect); the
// Progressor protocol has its own battery command.
constinit std::atomic<std::uint8_t> battery_level{ 100 };

void update_battery_service( k_work * /*work*/ )
{
    // On the system work queue, where a notification never waits for a buffer.
    if( int const err = bt_bas_set_battery_level( battery_level.load( std::memory_order_relaxed ) ); err != 0 && err != -ENOTCONN ) {
        LOG_DBG( "battery level not updated: %d", err );
    }
}

/** @brief zbus listener on channels::battery: runs in the publisher's thread, so it only hands the level on. */
void on_battery( const zbus_channel *chan );

} // namespace

K_WORK_DEFINE( ble_svc_battery_work, update_battery_service );
ZBUS_LISTENER_DEFINE( ble_svc_battery_listener, on_battery );
ZBUS_CHAN_ADD_OBS( battery_chan, ble_svc_battery_listener, 0 );

namespace
{

void on_battery( const zbus_channel * /*chan*/ )
{
    battery_level.store( ble_svc::battery_level_percent( channels::battery.const_msg().millivolts ), std::memory_order_relaxed );
    k_work_submit( &ble_svc_battery_work );
}

} // namespace

// The connection object is only free for a new connection once recycled; restart advertising from a work item then.
K_WORK_DEFINE( ble_svc_adv_work, restart_advertising );

BT_CONN_CB_DEFINE( ble_svc_conn_callbacks ) = {
    .connected = on_connected,
    .disconnected = on_disconnected,
    .recycled = [] { k_work_submit( &ble_svc_adv_work ); },
    .le_param_updated = on_param_updated,
};

namespace ble_svc
{

fw::Result<> init()
{
    name = advertised_name( identity_svc::identity().id() );

    if( auto ret = fw::err::check( bt_enable( nullptr ) ); !ret ) {
        LOG_ERR( "bt_enable failed: %d", fw::err::to_zephyr( ret.error() ) );
        return ret;
    }
    // The GAP device name, which clients read after connecting.
    if( auto ret = fw::err::check( bt_set_name( name.data() ) ); !ret ) {
        LOG_ERR( "bt_set_name failed: %d", fw::err::to_zephyr( ret.error() ) );
        return ret;
    }
    bt_gatt_cb_register( &gatt_callbacks );

    if( auto ret = start_fast_advertising(); !ret ) {
        LOG_ERR( "advertising start failed: %d", fw::err::to_zephyr( ret.error() ) );
        return ret;
    }
    LOG_INF( "advertising as \"%s\"", name.data() );
    return {};
}

IClient &client()
{
    return instance;
}

} // namespace ble_svc
