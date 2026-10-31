/**
 * @file test_settings.cpp
 * @brief Unit tests for fw::settings against real Zephyr settings (ZMS on native_sim's flash simulator).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <array>
#include <cstdint>
#include <fw/settings.hpp>
#include <gtest/gtest.h>
#include <system_error>

namespace
{

struct Point {
    float raw;
    float kg;
};

class SettingsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE( fw::settings::init() );
    }

    void TearDown() override
    {
        for( const char *key : { "test/factor", "test/point", "test/id", "test/id_suffix" } ) {
            (void)fw::settings::erase( key );
        }
    }
};

TEST_F( SettingsTest, LoadFailsWhenNothingStored )
{
    auto value = fw::settings::load<float>( "test/factor" );

    ASSERT_FALSE( value );
    EXPECT_EQ( value.error(), std::errc::no_such_file_or_directory );
}

TEST_F( SettingsTest, SaveThenLoadRoundTrips )
{
    ASSERT_TRUE( fw::settings::save( "test/factor", 0.109F ) );
    ASSERT_TRUE( fw::settings::save( "test/point", Point{ .raw = 1234.5F, .kg = 20.0F } ) );

    auto factor = fw::settings::load<float>( "test/factor" );
    auto point = fw::settings::load<Point>( "test/point" );

    ASSERT_TRUE( factor );
    EXPECT_FLOAT_EQ( *factor, 0.109F );
    ASSERT_TRUE( point );
    EXPECT_FLOAT_EQ( point->raw, 1234.5F );
    EXPECT_FLOAT_EQ( point->kg, 20.0F );
}

TEST_F( SettingsTest, SaveOverwrites )
{
    ASSERT_TRUE( fw::settings::save( "test/factor", 1.0F ) );
    ASSERT_TRUE( fw::settings::save( "test/factor", 2.0F ) );

    EXPECT_FLOAT_EQ( *fw::settings::load<float>( "test/factor" ), 2.0F );
}

TEST_F( SettingsTest, LoadRejectsValueOfAnotherSize )
{
    ASSERT_TRUE( fw::settings::save( "test/factor", std::uint32_t{ 7 } ) );

    auto value = fw::settings::load<std::uint16_t>( "test/factor" );

    ASSERT_FALSE( value );
    EXPECT_EQ( value.error(), std::errc::invalid_argument );
}

TEST_F( SettingsTest, EraseRemovesValue )
{
    ASSERT_TRUE( fw::settings::save( "test/factor", 1.0F ) );

    ASSERT_TRUE( fw::settings::erase( "test/factor" ) );

    EXPECT_EQ( fw::settings::load<float>( "test/factor" ).error(), std::errc::no_such_file_or_directory );
}

TEST_F( SettingsTest, KeysWithACommonPrefixAreSeparate )
{
    using Id = std::array<std::uint8_t, 6>;
    Id const id{ 1, 2, 3, 4, 5, 6 };
    ASSERT_TRUE( fw::settings::save( "test/id", id ) );
    ASSERT_TRUE( fw::settings::save( "test/id_suffix", std::uint8_t{ 9 } ) );

    EXPECT_EQ( *fw::settings::load<Id>( "test/id" ), id );
    EXPECT_EQ( *fw::settings::load<std::uint8_t>( "test/id_suffix" ), 9 );
}

} // namespace
