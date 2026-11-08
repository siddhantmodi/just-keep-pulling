/**
 * @file test_gauge.cpp
 * @brief Unit tests of power_manager::Gauge against a mocked fuel gauge, with real settings.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "gauge.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <expected>
#include <fw/settings.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <hal/mocks/mock_fuel_gauge.hpp>
#include <span>
#include <system_error>

namespace
{

using namespace std::chrono_literals;
using power_manager::gauge_state_key;
using testing::_;
using testing::NiceMock;
using testing::Return;

constexpr hal::BatteryStatus reading{ .millivolts = 3800 };
constexpr std::array<std::byte, 4> saved{ std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 }, std::byte{ 4 } };

auto is_empty()
{
    return testing::Truly( []( std::span<const std::byte> state ) { return state.empty(); } );
}

auto holds_saved()
{
    return testing::Truly( []( std::span<const std::byte> state ) { return std::ranges::equal( state, saved ); } );
}

class GaugeTest : public testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE( fw::settings::init() );
        (void)fw::settings::erase( gauge_state_key );
        ON_CALL( fuel_, state_size() ).WillByDefault( Return( saved.size() ) );
        ON_CALL( fuel_, update( _, _ ) ).WillByDefault( Return( 50.0F ) );
        ON_CALL( fuel_, save_state( _ ) )
            .WillByDefault(
                []( std::span<std::byte> out )
                {
                    std::ranges::copy( saved, out.begin() );
                    return fw::Result<>{};
                } );
    }

    NiceMock<hal::mocks::MockFuelGauge> fuel_;
    power_manager::Gauge gauge_{ fuel_ };
};

TEST_F( GaugeTest, StartsFromTheFirstReadingWithoutASavedState )
{
    EXPECT_CALL( fuel_, init( _, is_empty() ) );
    EXPECT_CALL( fuel_, update( _, 0ms ) ).WillOnce( Return( 42.4F ) );

    EXPECT_EQ( gauge_.update( reading, 5s ), 42 );
}

TEST_F( GaugeTest, ResumesFromTheSavedState )
{
    ASSERT_TRUE( fw::settings::save_bytes( gauge_state_key, saved ) );
    EXPECT_CALL( fuel_, init( _, holds_saved() ) );

    ASSERT_TRUE( gauge_.update( reading, 0s ) );
}

TEST_F( GaugeTest, StartsFreshWhenTheSavedStateIsUnusable )
{
    ASSERT_TRUE( fw::settings::save_bytes( gauge_state_key, saved ) );
    testing::InSequence const in_order;
    EXPECT_CALL( fuel_, init( _, holds_saved() ) ).WillOnce( Return( std::unexpected( std::errc::invalid_argument ) ) );
    EXPECT_CALL( fuel_, init( _, is_empty() ) );

    ASSERT_TRUE( gauge_.update( reading, 0s ) );
}

TEST_F( GaugeTest, IgnoresASavedStateOfAnotherSize )
{
    ASSERT_TRUE( fw::settings::save_bytes( gauge_state_key, std::span{ saved }.first( 3 ) ) );
    EXPECT_CALL( fuel_, init( _, is_empty() ) );

    ASSERT_TRUE( gauge_.update( reading, 0s ) );
}

TEST_F( GaugeTest, FailsWhenTheGaugeCannotStart )
{
    EXPECT_CALL( fuel_, init( _, _ ) ).WillOnce( Return( std::unexpected( std::errc::io_error ) ) );
    EXPECT_CALL( fuel_, update( _, _ ) ).Times( 0 );

    EXPECT_EQ( gauge_.update( reading, 0s ), std::unexpected( std::errc::io_error ) );
}

TEST_F( GaugeTest, PassesTheElapsedTimeAfterTheFirstReading )
{
    testing::InSequence const in_order;
    EXPECT_CALL( fuel_, update( _, 0ms ) );
    EXPECT_CALL( fuel_, update( _, 5000ms ) );

    ASSERT_TRUE( gauge_.update( reading, 7s ) );
    ASSERT_TRUE( gauge_.update( reading, 5s ) );
}

TEST_F( GaugeTest, RoundsAndClampsTheLevel )
{
    EXPECT_CALL( fuel_, update( _, _ ) ).WillOnce( Return( 49.5F ) ).WillOnce( Return( 100.6F ) ).WillOnce( Return( -2.0F ) );

    EXPECT_EQ( gauge_.update( reading, 0s ), 50 );
    EXPECT_EQ( gauge_.update( reading, 5s ), 100 );
    EXPECT_EQ( gauge_.update( reading, 5s ), 0 );
}

TEST_F( GaugeTest, SavesTheStateEveryInterval )
{
    ASSERT_TRUE( gauge_.update( reading, 0s ) );
    EXPECT_CALL( fuel_, save_state( _ ) ).Times( 0 );
    for( auto elapsed = 5s; elapsed < power_manager::gauge_save_interval; elapsed += 5s ) {
        ASSERT_TRUE( gauge_.update( reading, 5s ) );
    }
    testing::Mock::VerifyAndClearExpectations( &fuel_ );

    EXPECT_CALL( fuel_, save_state( _ ) );
    ASSERT_TRUE( gauge_.update( reading, 5s ) );

    std::array<std::byte, saved.size()> stored{};
    ASSERT_TRUE( fw::settings::load_bytes( gauge_state_key, stored ) );
    EXPECT_EQ( stored, saved );
}

TEST_F( GaugeTest, SaveWritesTheStateToSettings )
{
    ASSERT_TRUE( gauge_.update( reading, 0s ) );
    ASSERT_TRUE( gauge_.save() );

    std::array<std::byte, saved.size()> stored{};
    ASSERT_TRUE( fw::settings::load_bytes( gauge_state_key, stored ) );
    EXPECT_EQ( stored, saved );
}

TEST_F( GaugeTest, NothingToSaveBeforeTheFirstReadingOrWithoutState )
{
    EXPECT_CALL( fuel_, save_state( _ ) ).Times( 0 );
    EXPECT_TRUE( gauge_.save() );

    ON_CALL( fuel_, state_size() ).WillByDefault( Return( 0 ) );
    ASSERT_TRUE( gauge_.update( reading, 0s ) );
    EXPECT_TRUE( gauge_.save() );
}

} // namespace
