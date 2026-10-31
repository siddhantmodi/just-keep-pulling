/**
 * @file test_identity.cpp
 * @brief Unit tests for identity_svc: the ID's text form, generation on first boot and overwriting.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "identity.hpp"

#include <fw/settings.hpp>
#include <gtest/gtest.h>
#include <string_view>
#include <system_error>

using identity_svc::DeviceId;

namespace
{

TEST( DeviceIdTextTest, ParsesTwelveHexDigitsInEitherCase )
{
    auto id = identity_svc::parse_device_id( "7e4E17010a3C" );

    ASSERT_TRUE( id );
    EXPECT_EQ( *id, ( DeviceId{ 0x7E, 0x4E, 0x17, 0x01, 0x0A, 0x3C } ) );
}

TEST( DeviceIdTextTest, RejectsWrongLengthOrNonHex )
{
    for( std::string_view const text : { "", "7E4E17010A3", "7E4E17010A3C0", "7E4E17010A3G", "7E:4E:17:01" } ) {
        EXPECT_EQ( identity_svc::parse_device_id( text ).error(), std::errc::invalid_argument ) << text;
    }
}

TEST( DeviceIdTextTest, FormatsUpperCaseHex )
{
    EXPECT_STREQ( identity_svc::format_device_id( DeviceId{ 0x7E, 0x4E, 0x17, 0x01, 0x0A, 0x3C } ).data(), "7E4E17010A3C" );
}

class IdentityTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE( fw::settings::init() );
        (void)fw::settings::erase( identity_svc::id_key );
    }

    void TearDown() override
    {
        (void)fw::settings::erase( identity_svc::id_key );
    }
};

TEST_F( IdentityTest, FirstBootGeneratesAndSavesAnId )
{
    identity_svc::Identity identity;

    ASSERT_TRUE( identity.init() );

    EXPECT_NE( identity.id(), DeviceId{} );
    auto saved = fw::settings::load<DeviceId>( identity_svc::id_key );
    ASSERT_TRUE( saved );
    EXPECT_EQ( *saved, identity.id() );
}

TEST_F( IdentityTest, LaterBootsKeepTheSameId )
{
    identity_svc::Identity first_boot;
    ASSERT_TRUE( first_boot.init() );

    identity_svc::Identity second_boot;
    ASSERT_TRUE( second_boot.init() );

    EXPECT_EQ( second_boot.id(), first_boot.id() );
}

TEST_F( IdentityTest, BlankSavedIdIsReplaced )
{
    ASSERT_TRUE( fw::settings::save( identity_svc::id_key, DeviceId{} ) );
    identity_svc::Identity identity;

    ASSERT_TRUE( identity.init() );

    EXPECT_NE( identity.id(), DeviceId{} );
}

TEST_F( IdentityTest, SetIdSavesItForLaterBoots )
{
    identity_svc::Identity identity;
    ASSERT_TRUE( identity.init() );
    DeviceId const chosen{ 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC };

    ASSERT_TRUE( identity.set_id( chosen ) );

    EXPECT_EQ( identity.id(), chosen );
    identity_svc::Identity next_boot;
    ASSERT_TRUE( next_boot.init() );
    EXPECT_EQ( next_boot.id(), chosen );
}

TEST_F( IdentityTest, SetIdRejectsBlankIds )
{
    identity_svc::Identity identity;
    ASSERT_TRUE( identity.init() );
    DeviceId const before = identity.id();

    EXPECT_EQ( identity.set_id( DeviceId{} ).error(), std::errc::invalid_argument );
    EXPECT_EQ( identity.set_id( DeviceId{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } ).error(), std::errc::invalid_argument );
    EXPECT_EQ( identity.id(), before );
}

} // namespace
