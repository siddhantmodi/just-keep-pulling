/**
 * @file test_die_temperature.cpp
 * @brief Unit tests for temp_svc::DieTemperature with a mocked hal::ITemperatureSensor.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "die_temperature.hpp"

#include <zephyr/kernel.h>

#include <cstdint>
#include <expected>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <hal/mocks/mock_temperature_sensor.hpp>
#include <limits>
#include <system_error>

using ::hal::mocks::MockTemperatureSensor;
using ::testing::NiceMock;
using ::testing::Return;

K_MUTEX_DEFINE( test_read_mutex );

namespace
{

class DieTemperatureTest : public ::testing::Test
{
protected:
    NiceMock<MockTemperatureSensor> sensor_;
    temp_svc::DieTemperature die_{ sensor_, test_read_mutex };
};

TEST_F( DieTemperatureTest, InitPropagatesSensorError )
{
    EXPECT_CALL( sensor_, init() ).WillOnce( Return( fw::Result<>{ std::unexpected( std::errc::no_such_device ) } ) );

    auto ret = die_.init();

    ASSERT_FALSE( ret );
    EXPECT_EQ( ret.error(), std::errc::no_such_device );
}

TEST_F( DieTemperatureTest, ReadBeforeInitIsRejectedWithoutTouchingSensor )
{
    EXPECT_CALL( sensor_, read_millicelsius() ).Times( 0 );

    auto ret = die_.read_centicelsius();

    ASSERT_FALSE( ret );
    EXPECT_EQ( ret.error(), std::errc::operation_not_permitted );
}

TEST_F( DieTemperatureTest, ReadConvertsToCentidegrees )
{
    ASSERT_TRUE( die_.init() );
    EXPECT_CALL( sensor_, read_millicelsius() ).WillOnce( Return( fw::Result<std::int32_t>{ 25370 } ) );

    auto ret = die_.read_centicelsius();

    ASSERT_TRUE( ret );
    EXPECT_EQ( *ret, 2537 );
}

TEST_F( DieTemperatureTest, ReadHoldsMutexWhileReadingSensor )
{
    ASSERT_TRUE( die_.init() );
    EXPECT_CALL( sensor_, read_millicelsius() )
        .WillOnce(
            []
            {
                EXPECT_EQ( test_read_mutex.lock_count, 1U );
                return fw::Result<std::int32_t>{ 0 };
            } );

    ASSERT_TRUE( die_.read_centicelsius() );
    EXPECT_EQ( test_read_mutex.lock_count, 0U );
}

TEST_F( DieTemperatureTest, ReadPropagatesSensorError )
{
    ASSERT_TRUE( die_.init() );
    EXPECT_CALL( sensor_, read_millicelsius() ).WillOnce( Return( fw::Result<std::int32_t>{ std::unexpected( std::errc::io_error ) } ) );

    auto ret = die_.read_centicelsius();

    ASSERT_FALSE( ret );
    EXPECT_EQ( ret.error(), std::errc::io_error );
}

TEST( ToCenticelsiusTest, RoundsHalfAwayFromZero )
{
    EXPECT_EQ( temp_svc::to_centicelsius( 0 ), 0 );
    EXPECT_EQ( temp_svc::to_centicelsius( 25374 ), 2537 );
    EXPECT_EQ( temp_svc::to_centicelsius( 25375 ), 2538 );
    EXPECT_EQ( temp_svc::to_centicelsius( -1234 ), -123 );
    EXPECT_EQ( temp_svc::to_centicelsius( -1235 ), -124 );
}

TEST( ToCenticelsiusTest, AcceptsInt16Limits )
{
    EXPECT_EQ( temp_svc::to_centicelsius( 327670 ), std::numeric_limits<std::int16_t>::max() );
    EXPECT_EQ( temp_svc::to_centicelsius( -327680 ), std::numeric_limits<std::int16_t>::min() );
}

TEST( ToCenticelsiusTest, RejectsValuesOutsideInt16 )
{
    EXPECT_EQ( temp_svc::to_centicelsius( 327675 ).error(), std::errc::result_out_of_range );
    EXPECT_EQ( temp_svc::to_centicelsius( std::numeric_limits<std::int32_t>::min() ).error(), std::errc::result_out_of_range );
    EXPECT_EQ( temp_svc::to_centicelsius( std::numeric_limits<std::int32_t>::max() ).error(), std::errc::result_out_of_range );
}

static_assert( temp_svc::to_centicelsius( 21500 ) == 2150 );

} // namespace
