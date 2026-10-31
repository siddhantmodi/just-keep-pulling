/**
 * @file test_advertising.cpp
 * @brief Unit tests of the advertised name.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "advertising.hpp"

#include <gtest/gtest.h>
#include <string_view>

namespace
{

std::string_view as_string( const std::array<char, ble_svc::name_size> &name )
{
    return { name.data() };
}

TEST( AdvertisedName, IsProgressorAndTheLastTwoBytesInDecimal )
{
    EXPECT_EQ( as_string( ble_svc::advertised_name( { 0x7E, 0x4E, 0x17, 0x01, 0x1B, 0xD5 } ) ), "Progressor_7125" ); // 0x1BD5 = 7125
}

TEST( AdvertisedName, PadsWithZeros )
{
    EXPECT_EQ( as_string( ble_svc::advertised_name( { 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x07 } ) ), "Progressor_0007" );
}

TEST( AdvertisedName, KeepsFourDigits )
{
    EXPECT_EQ( as_string( ble_svc::advertised_name( { 0, 0, 0, 0, 0xFF, 0xFF } ) ), "Progressor_5535" ); // 65535 % 10000
}

TEST( AdvertisedName, IgnoresTheOtherBytes )
{
    EXPECT_EQ( ble_svc::advertised_name( { 1, 2, 3, 4, 0x12, 0x34 } ), ble_svc::advertised_name( { 9, 8, 7, 6, 0x12, 0x34 } ) );
}

TEST( AdvertisedName, IsNulTerminatedAndFitsLegacyAdvertising )
{
    auto const name = ble_svc::advertised_name( { 0, 0, 0, 0, 0, 0 } );
    EXPECT_EQ( name.back(), '\0' );
    // Flags (3 bytes) and the name's 2-byte header leave 26 bytes of the 31.
    static_assert( ble_svc::name_size - 1 <= 26 );
}

static_assert( ble_svc::advertised_name( { 0, 0, 0, 0, 0x04, 0xD2 } )[14] == '4', "usable at compile time" );

} // namespace
