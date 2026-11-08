/**
 * @file test_power.cpp
 * @brief Unit tests of power_manager::Power against a mocked PMIC, client and fuel gauge, with real zbus channels and settings.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "channels.hpp"
#include "mock_client.hpp"
#include "mock_pmic.hpp"
#include "power.hpp"

#include <chrono>
#include <cstdint>
#include <expected>
#include <fw/settings.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <hal/mocks/mock_fuel_gauge.hpp>
#include <span>
#include <system_error>
#include <vector>

namespace
{

using namespace std::chrono_literals;
using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;

constexpr hal::BatteryStatus healthy{ .millivolts = 3900, .external_power = false };
constexpr hal::BatteryStatus low{ .millivolts = 3300, .external_power = false };

class PowerTest : public testing::Test
{
protected:
    PowerTest()
    {
        ON_CALL( fuel_, update( _, _ ) ).WillByDefault( Return( 50.0F ) );
        ON_CALL( client_, send_progressor( _ ) )
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

    void SetUp() override
    {
        ASSERT_TRUE( fw::settings::init() );
    }

    StrictMock<pmic_svc::MockPmic> pmic_;
    NiceMock<ble_svc::MockClient> client_;
    NiceMock<hal::mocks::MockFuelGauge> fuel_;
    power_manager::Gauge gauge_{ fuel_ };
    power_manager::Power power_{ pmic_, client_, gauge_ };
    std::vector<std::vector<std::uint8_t>> sent_;
};

TEST( IsLow, AtOrBelowTheThresholdOnBatteryAlone )
{
    EXPECT_TRUE( power_manager::is_low( { .millivolts = 3300, .external_power = false } ) );
    EXPECT_TRUE( power_manager::is_low( { .millivolts = 3000, .external_power = false } ) );
    EXPECT_FALSE( power_manager::is_low( { .millivolts = 3301, .external_power = false } ) );
}

TEST( IsLow, NotWhileChargingOrOnUsb )
{
    EXPECT_FALSE( power_manager::is_low( { .millivolts = 3200, .charge = hal::ChargeState::constant_current, .external_power = true } ) );
    EXPECT_FALSE( power_manager::is_low( { .millivolts = 3200, .external_power = true } ) );
    EXPECT_FALSE( power_manager::is_low( { .millivolts = 3200, .charge = hal::ChargeState::constant_current, .external_power = false } ) );
}

TEST_F( PowerTest, PublishesEachReading )
{
    EXPECT_CALL( pmic_, read_battery() ).WillOnce( Return( healthy ) );

    ASSERT_TRUE( power_.check() );

    auto published = channels::battery.read( K_NO_WAIT );
    ASSERT_TRUE( published );
    EXPECT_EQ( published->status.millivolts, 3900U );
    EXPECT_EQ( published->percent, 50 );
    EXPECT_EQ( power_.next_check(), power_manager::check_interval );
}

TEST_F( PowerTest, GivesTheGaugeTheTimeSinceTheLastReading )
{
    EXPECT_CALL( pmic_, read_battery() )
        .WillOnce( Return( healthy ) )
        .WillOnce( Return( healthy ) )
        .WillOnce( Return( std::unexpected( std::errc::io_error ) ) )
        .WillOnce( Return( healthy ) );
    testing::InSequence const in_order;
    EXPECT_CALL( fuel_, update( _, 0ms ) );
    EXPECT_CALL( fuel_, update( _, std::chrono::milliseconds{ power_manager::check_interval } ) );
    EXPECT_CALL( fuel_, update( _, 2 * power_manager::check_interval ) ); // across the failed read

    ASSERT_TRUE( power_.check() );
    ASSERT_TRUE( power_.check() );
    ASSERT_FALSE( power_.check() );
    ASSERT_TRUE( power_.check() );
}

TEST_F( PowerTest, KeepsTheLastLevelWhenTheGaugeFails )
{
    battery_reads( healthy );
    EXPECT_CALL( pmic_, read_battery() ).Times( 2 );
    EXPECT_CALL( fuel_, update( _, _ ) ).WillOnce( Return( 72.0F ) ).WillOnce( Return( std::unexpected( std::errc::io_error ) ) );

    ASSERT_TRUE( power_.check() );
    ASSERT_TRUE( power_.check() );

    auto published = channels::battery.read( K_NO_WAIT );
    ASSERT_TRUE( published );
    EXPECT_EQ( published->percent, 72 );
}

TEST_F( PowerTest, AFailedReadIsRetriedAtTheNextInterval )
{
    EXPECT_CALL( pmic_, read_battery() ).WillOnce( Return( std::unexpected( std::errc::io_error ) ) );

    EXPECT_EQ( power_.check(), std::unexpected( std::errc::io_error ) );
    EXPECT_EQ( power_.next_check(), power_manager::check_interval );
}

TEST_F( PowerTest, ALowReadingIsConfirmedSoon )
{
    EXPECT_CALL( pmic_, read_battery() ).WillOnce( Return( low ) );

    ASSERT_TRUE( power_.check() );

    EXPECT_EQ( power_.next_check(), power_manager::confirm_delay );
    EXPECT_TRUE( sent_.empty() );
}

TEST_F( PowerTest, ARecoveredReadingCancelsTheConfirmation )
{
    EXPECT_CALL( pmic_, read_battery() ).WillOnce( Return( low ) ).WillOnce( Return( healthy ) ).WillOnce( Return( healthy ) );

    ASSERT_TRUE( power_.check() );
    ASSERT_TRUE( power_.check() );
    EXPECT_EQ( power_.next_check(), power_manager::check_interval );
    EXPECT_TRUE( sent_.empty() );

    ASSERT_TRUE( power_.check() ); // no ship mode: StrictMock would fail
}

TEST_F( PowerTest, AConfirmedLowBatteryWarnsTheClientThenEntersShipMode )
{
    battery_reads( low );
    EXPECT_CALL( pmic_, read_battery() ).Times( 2 );

    ASSERT_TRUE( power_.check() );
    ASSERT_TRUE( power_.check() );

    ASSERT_EQ( sent_.size(), 1U );
    EXPECT_EQ( sent_[0], ( std::vector<std::uint8_t>{ 0x04, 0x00 } ) ); // code, zero length
    EXPECT_EQ( power_.next_check(), power_manager::warning_delay );

    EXPECT_CALL( pmic_, ship_mode() ).WillOnce( Return( fw::Result<>{} ) );
    EXPECT_TRUE( power_.check() );
}

TEST_F( PowerTest, SavesTheGaugeBeforeShipMode )
{
    battery_reads( low );
    EXPECT_CALL( pmic_, read_battery() ).Times( 2 );
    ON_CALL( fuel_, state_size() ).WillByDefault( Return( 4 ) );
    ASSERT_TRUE( power_.check() );
    ASSERT_TRUE( power_.check() );

    testing::InSequence const in_order;
    EXPECT_CALL( fuel_, save_state( _ ) ).WillOnce( Return( fw::Result<>{} ) );
    EXPECT_CALL( pmic_, ship_mode() ).WillOnce( Return( fw::Result<>{} ) );
    EXPECT_TRUE( power_.check() );
}

TEST_F( PowerTest, PowersOffWithoutAClient )
{
    battery_reads( low );
    EXPECT_CALL( pmic_, read_battery() ).Times( 2 );
    ON_CALL( client_, send_progressor( _ ) ).WillByDefault( Return( std::unexpected( std::errc::not_connected ) ) );

    ASSERT_TRUE( power_.check() );
    ASSERT_TRUE( power_.check() );

    EXPECT_CALL( pmic_, ship_mode() ).WillOnce( Return( fw::Result<>{} ) );
    EXPECT_TRUE( power_.check() );
}

TEST_F( PowerTest, AFailedShipModeGoesBackToMonitoring )
{
    battery_reads( low );
    EXPECT_CALL( pmic_, read_battery() ).Times( 3 );
    ASSERT_TRUE( power_.check() );
    ASSERT_TRUE( power_.check() );
    EXPECT_CALL( pmic_, ship_mode() ).WillOnce( Return( std::unexpected( std::errc::io_error ) ) );

    EXPECT_EQ( power_.check(), std::unexpected( std::errc::io_error ) );
    EXPECT_EQ( power_.next_check(), power_manager::check_interval );

    // Starts over: the next low reading is confirmed again before powering off.
    ASSERT_TRUE( power_.check() );
    EXPECT_EQ( power_.next_check(), power_manager::confirm_delay );
}

TEST_F( PowerTest, ChargingNeverPowersOff )
{
    battery_reads( { .millivolts = 3100, .charge = hal::ChargeState::constant_current, .external_power = true } );
    EXPECT_CALL( pmic_, read_battery() ).Times( 3 );

    for( int i = 0; i < 3; ++i ) {
        ASSERT_TRUE( power_.check() );
        EXPECT_EQ( power_.next_check(), power_manager::check_interval );
    }
    EXPECT_TRUE( sent_.empty() );
}

} // namespace
