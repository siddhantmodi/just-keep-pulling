/**
 * @file test_power.cpp
 * @brief Unit tests of power_manager::Power against a mocked PMIC and client, with real zbus channels.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "channels.hpp"
#include "mock_client.hpp"
#include "mock_pmic.hpp"
#include "power.hpp"

#include <cstdint>
#include <expected>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <span>
#include <system_error>
#include <vector>

namespace
{

using power_manager::Event;
using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;

constexpr Event check{ .type = Event::Type::battery_check };
constexpr hal::BatteryStatus healthy{ .millivolts = 3900, .charging = false, .external_power = false };
constexpr hal::BatteryStatus low{ .millivolts = 3300, .charging = false, .external_power = false };

class PowerTest : public testing::Test
{
protected:
    PowerTest()
    {
        ON_CALL( client_, send( _ ) )
            .WillByDefault(
                [this]( std::span<const std::uint8_t> bytes )
                {
                    sent_.emplace_back( bytes.begin(), bytes.end() );
                    return fw::Result<>{};
                } );
    }

    void battery_reads( hal::BatteryStatus status )
    {
        ON_CALL( pmic_, read_battery() ).WillByDefault( Return( status ) );
    }

    StrictMock<pmic_svc::MockPmic> pmic_;
    NiceMock<ble_svc::MockClient> client_;
    power_manager::Power power_{ pmic_, client_ };
    std::vector<std::vector<std::uint8_t>> sent_;
};

TEST( IsLow, AtOrBelowTheThresholdOnBatteryAlone )
{
    EXPECT_TRUE( power_manager::is_low( { .millivolts = 3300, .charging = false, .external_power = false } ) );
    EXPECT_TRUE( power_manager::is_low( { .millivolts = 3000, .charging = false, .external_power = false } ) );
    EXPECT_FALSE( power_manager::is_low( { .millivolts = 3301, .charging = false, .external_power = false } ) );
}

TEST( IsLow, NotWhileChargingOrOnUsb )
{
    EXPECT_FALSE( power_manager::is_low( { .millivolts = 3200, .charging = true, .external_power = true } ) );
    EXPECT_FALSE( power_manager::is_low( { .millivolts = 3200, .charging = false, .external_power = true } ) );
    EXPECT_FALSE( power_manager::is_low( { .millivolts = 3200, .charging = true, .external_power = false } ) );
}

TEST_F( PowerTest, PublishesEachReading )
{
    EXPECT_CALL( pmic_, read_battery() ).WillOnce( Return( healthy ) );

    ASSERT_TRUE( power_.handle( check ) );

    auto published = channels::battery.read();
    ASSERT_TRUE( published );
    EXPECT_EQ( published->millivolts, 3900U );
    EXPECT_EQ( power_.next_check(), power_manager::check_interval );
}

TEST_F( PowerTest, AFailedReadIsRetriedAtTheNextInterval )
{
    EXPECT_CALL( pmic_, read_battery() ).WillOnce( Return( std::unexpected( std::errc::io_error ) ) );

    EXPECT_EQ( power_.handle( check ), std::unexpected( std::errc::io_error ) );
    EXPECT_EQ( power_.next_check(), power_manager::check_interval );
}

TEST_F( PowerTest, ALowReadingIsConfirmedSoon )
{
    EXPECT_CALL( pmic_, read_battery() ).WillOnce( Return( low ) );

    ASSERT_TRUE( power_.handle( check ) );

    EXPECT_EQ( power_.next_check(), power_manager::confirm_delay );
    EXPECT_TRUE( sent_.empty() );
}

TEST_F( PowerTest, ARecoveredReadingCancelsTheConfirmation )
{
    EXPECT_CALL( pmic_, read_battery() ).WillOnce( Return( low ) ).WillOnce( Return( healthy ) ).WillOnce( Return( healthy ) );

    ASSERT_TRUE( power_.handle( check ) );
    ASSERT_TRUE( power_.handle( check ) );
    EXPECT_EQ( power_.next_check(), power_manager::check_interval );
    EXPECT_TRUE( sent_.empty() );

    ASSERT_TRUE( power_.handle( check ) ); // no ship mode: StrictMock would fail
}

TEST_F( PowerTest, AConfirmedLowBatteryWarnsTheClientThenEntersShipMode )
{
    battery_reads( low );
    EXPECT_CALL( pmic_, read_battery() ).Times( 2 );

    ASSERT_TRUE( power_.handle( check ) );
    ASSERT_TRUE( power_.handle( check ) );

    ASSERT_EQ( sent_.size(), 1U );
    EXPECT_EQ( sent_[0], ( std::vector<std::uint8_t>{ 0x04, 0x00 } ) ); // code, zero length
    EXPECT_EQ( power_.next_check(), power_manager::warning_delay );

    EXPECT_CALL( pmic_, ship_mode() ).WillOnce( Return( fw::Result<>{} ) );
    EXPECT_TRUE( power_.handle( check ) );
}

TEST_F( PowerTest, PowersOffWithoutAClient )
{
    battery_reads( low );
    EXPECT_CALL( pmic_, read_battery() ).Times( 2 );
    ON_CALL( client_, send( _ ) ).WillByDefault( Return( std::unexpected( std::errc::not_connected ) ) );

    ASSERT_TRUE( power_.handle( check ) );
    ASSERT_TRUE( power_.handle( check ) );

    EXPECT_CALL( pmic_, ship_mode() ).WillOnce( Return( fw::Result<>{} ) );
    EXPECT_TRUE( power_.handle( check ) );
}

TEST_F( PowerTest, AFailedShipModeGoesBackToMonitoring )
{
    battery_reads( low );
    EXPECT_CALL( pmic_, read_battery() ).Times( 3 );
    ASSERT_TRUE( power_.handle( check ) );
    ASSERT_TRUE( power_.handle( check ) );
    EXPECT_CALL( pmic_, ship_mode() ).WillOnce( Return( std::unexpected( std::errc::io_error ) ) );

    EXPECT_EQ( power_.handle( check ), std::unexpected( std::errc::io_error ) );
    EXPECT_EQ( power_.next_check(), power_manager::check_interval );

    // Starts over: the next low reading is confirmed again before powering off.
    ASSERT_TRUE( power_.handle( check ) );
    EXPECT_EQ( power_.next_check(), power_manager::confirm_delay );
}

TEST_F( PowerTest, ChargingNeverPowersOff )
{
    battery_reads( { .millivolts = 3100, .charging = true, .external_power = true } );
    EXPECT_CALL( pmic_, read_battery() ).Times( 3 );

    for( int i = 0; i < 3; ++i ) {
        ASSERT_TRUE( power_.handle( check ) );
        EXPECT_EQ( power_.next_check(), power_manager::check_interval );
    }
    EXPECT_TRUE( sent_.empty() );
}

} // namespace
