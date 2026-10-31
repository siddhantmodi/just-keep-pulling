/**
 * @file test_ads1220.cpp
 * @brief hal::zephyr::Ads1220 and the ADS1220 driver against the emulated chip (jkp,ads1220 on spi0).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <gtest/gtest.h>
#include <hal/zephyr/ads1220.hpp>
#include <jkp/drivers/ads1220.h>
#include <jkp/drivers/ads1220_emul.h>
#include <system_error>

using namespace std::chrono_literals;

namespace
{

const emul *const emulator = EMUL_DT_GET( DT_NODELABEL( load_cell_adc ) );

using hal::SampleRate;

std::int32_t ten_per_conversion( void * /*context*/, std::uint32_t index )
{
    return static_cast<std::int32_t>( index ) * 10;
}

class Ads1220Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE( adc_.init() );
    }

    void TearDown() override
    {
        (void)adc_.stop();
        ads1220_emul_set_raw( emulator, 0 );
    }

    /** @brief Long enough for a conversion at any rate (20 SPS = 50 ms). */
    static constexpr std::chrono::microseconds timeout = 100ms;

    hal::zephyr::Ads1220 adc_{ DEVICE_DT_GET( DT_NODELABEL( load_cell_adc ) ) };
};

TEST_F( Ads1220Test, StaysPoweredDownUntilStarted )
{
    EXPECT_FALSE( ads1220_emul_is_converting( emulator ) );
}

TEST_F( Ads1220Test, ConfiguresRatiometricBridgeInput )
{
    ASSERT_TRUE( adc_.start( SampleRate::sps175 ) );

    std::array<std::uint8_t, 4> registers{};
    ads1220_emul_get_registers( emulator, registers.data() );

    EXPECT_EQ( registers[0], 0x3E ); // AIN1 vs AIN2, gain 128, PGA enabled
    EXPECT_EQ( registers[1], 0x64 ); // 175 SPS, normal mode, continuous conversion
    EXPECT_EQ( registers[2], 0x48 ); // reference REFP0/REFN0, low-side power switch
    EXPECT_EQ( registers[3], 0x00 ); // /DRDY on its own pin
    EXPECT_TRUE( ads1220_emul_is_converting( emulator ) );
}

TEST_F( Ads1220Test, ReadReturnsSignedTwentyFourBitSamples )
{
    ASSERT_TRUE( adc_.start( SampleRate::sps1000 ) );

    for( std::int32_t const raw : { 0, 123456, -123456, ( 1 << 23 ) - 1, -( 1 << 23 ) } ) {
        ads1220_emul_set_raw( emulator, raw );
        (void)adc_.read( timeout ); // may hold a conversion latched before the change

        auto sample = adc_.read( timeout );

        ASSERT_TRUE( sample );
        EXPECT_EQ( sample->raw, raw );
    }
}

TEST_F( Ads1220Test, ReadsEveryConversionInOrderAtFullRate )
{
    ads1220_emul_set_source( emulator, ten_per_conversion, nullptr );
    ASSERT_TRUE( adc_.start( SampleRate::sps1000 ) );

    for( std::int32_t expected = 0; expected < 500; expected += 10 ) {
        auto sample = adc_.read( timeout );

        ASSERT_TRUE( sample );
        ASSERT_EQ( sample->raw, expected );
    }
}

TEST_F( Ads1220Test, TimestampsFollowConversionsNotReads )
{
    ASSERT_TRUE( adc_.start( SampleRate::sps1000 ) );
    auto first = adc_.read( timeout );
    ASSERT_TRUE( first );

    // Read the next conversion late: its timestamp is still when it completed.
    k_busy_wait( 400 );
    auto second = adc_.read( timeout );

    ASSERT_TRUE( second );
    EXPECT_EQ( second->timestamp_us - first->timestamp_us, 1000U );
}

TEST_F( Ads1220Test, TimestampsAreEvenlySpacedAtEveryRate )
{
    // The emulator's timer rounds its period up to whole ticks.
    constexpr std::uint64_t tick_us = 1'000'000U / CONFIG_SYS_CLOCK_TICKS_PER_SEC;

    for( SampleRate const rate : hal::all_sample_rates ) {
        ASSERT_TRUE( adc_.start( rate ) ) << hal::samples_per_second( rate ) << " SPS";
        auto first = adc_.read( timeout );
        auto second = adc_.read( timeout );
        ASSERT_TRUE( first && second );
        std::uint64_t const period = second->timestamp_us - first->timestamp_us;
        EXPECT_GE( period, 1'000'000U / hal::samples_per_second( rate ) ) << hal::samples_per_second( rate ) << " SPS";
        EXPECT_LE( period, ( 1'000'000U / hal::samples_per_second( rate ) ) + tick_us ) << hal::samples_per_second( rate ) << " SPS";

        auto previous = second;
        for( int i = 0; i < 5; ++i ) {
            auto sample = adc_.read( timeout );
            ASSERT_TRUE( sample );
            EXPECT_EQ( sample->timestamp_us - previous->timestamp_us, period ) << hal::samples_per_second( rate ) << " SPS";
            previous = sample;
        }
        ASSERT_TRUE( adc_.stop() );
    }
}

TEST_F( Ads1220Test, CountsSamplesOverwrittenBeforeBeingRead )
{
    ads1220_emul_set_source( emulator, ten_per_conversion, nullptr );
    ASSERT_TRUE( adc_.start( SampleRate::sps1000 ) );
    ASSERT_TRUE( adc_.read( timeout ) );
    EXPECT_EQ( adc_.lost_samples(), 0U );

    k_sleep( K_USEC( 5500 ) ); // five conversions complete; only the last can be read
    auto sample = adc_.read( timeout );

    ASSERT_TRUE( sample );
    EXPECT_EQ( adc_.lost_samples(), 4U );
    EXPECT_EQ( sample->raw, 50 ); // the newest conversion

    ASSERT_TRUE( adc_.start( SampleRate::sps1000 ) );
    EXPECT_EQ( adc_.lost_samples(), 0U ); // reset by start
}

TEST_F( Ads1220Test, SupportsEveryNormalModeRate )
{
    for( SampleRate const rate : hal::all_sample_rates ) {
        ASSERT_TRUE( adc_.start( rate ) ) << hal::samples_per_second( rate ) << " SPS";
        EXPECT_TRUE( adc_.read( timeout ) ) << hal::samples_per_second( rate ) << " SPS";
        ASSERT_TRUE( adc_.stop() );
    }
}

TEST( SampleRateTest, ConvertsFromSamplesPerSecond )
{
    for( SampleRate const rate : hal::all_sample_rates ) {
        EXPECT_EQ( hal::to_sample_rate( hal::samples_per_second( rate ) ), rate );
    }
    for( std::uint16_t const unsupported : { 0, 100, 176, 2000 } ) {
        EXPECT_EQ( hal::to_sample_rate( unsupported ).error(), std::errc::not_supported ) << unsupported;
    }
}

static_assert( hal::samples_per_second( SampleRate::sps175 ) == 175 );

TEST_F( Ads1220Test, DriverRejectsUnsupportedRate )
{
    // Only the C API can be given a rate outside SampleRate.
    EXPECT_EQ( ads1220_start( DEVICE_DT_GET( DT_NODELABEL( load_cell_adc ) ), 100 ), -ENOTSUP );
    EXPECT_FALSE( ads1220_emul_is_converting( emulator ) );
}

TEST_F( Ads1220Test, ReadFailsWhenNotStarted )
{
    auto sample = adc_.read( timeout );

    ASSERT_FALSE( sample );
    EXPECT_EQ( sample.error(), std::errc::operation_not_permitted );
}

TEST_F( Ads1220Test, ReadTimesOutBeforeNextConversion )
{
    ASSERT_TRUE( adc_.start( SampleRate::sps20 ) ); // one conversion every 50 ms

    auto sample = adc_.read( 1ms );

    ASSERT_FALSE( sample );
    EXPECT_EQ( sample.error(), std::errc::resource_unavailable_try_again );
}

TEST_F( Ads1220Test, StopPowersDown )
{
    ASSERT_TRUE( adc_.start( SampleRate::sps1000 ) );
    ASSERT_TRUE( adc_.read( timeout ) );

    ASSERT_TRUE( adc_.stop() );

    EXPECT_FALSE( ads1220_emul_is_converting( emulator ) );
    EXPECT_EQ( adc_.read( timeout ).error(), std::errc::operation_not_permitted );
}

TEST( Ads1220NoDeviceTest, InitFailsWithoutDevice )
{
    hal::zephyr::Ads1220 adc{ nullptr };

    EXPECT_EQ( adc.init().error(), std::errc::no_such_device );
}

} // namespace
