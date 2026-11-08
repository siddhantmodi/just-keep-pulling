/**
 * @file npm13xx_charger.hpp
 * @brief hal::zephyr::Npm13xxCharger: hal::IBatteryCharger over Zephyr's nPM13xx charger sensor driver.
 *
 * Constructed from the charger's device (the nordic,npm1300-charger node):
 * @code
 * constinit hal::zephyr::Npm13xxCharger charger{ DEVICE_DT_GET( DT_NODELABEL( pmic_charger ) ) };
 * @endcode
 *
 * Not thread-safe: a read is a sample fetch followed by channel gets, and the
 * driver holds the sample in between. Serialize reads from multiple threads.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/npm13xx_charger.h>

#include <cstdint>
#include <expected>
#include <fw/error.hpp>
#include <hal/battery_charger.hpp>
#include <initializer_list>
#include <system_error>

namespace hal::zephyr
{

class Npm13xxCharger final : public IBatteryCharger
{
public:
    constexpr explicit Npm13xxCharger( const device *dev ) noexcept
        : dev_{ dev }
    {
    }

    [[nodiscard]] fw::Result<> init() override
    {
        if( !device_is_ready( dev_ ) ) {
            return std::unexpected( std::errc::no_such_device );
        }
        return {};
    }

    [[nodiscard]] fw::Result<BatteryStatus> read() override
    {
        if( auto ret = fw::err::check( sensor_sample_fetch( dev_ ) ); !ret ) {
            return std::unexpected( ret.error() );
        }
        auto voltage = channel( SENSOR_CHAN_GAUGE_VOLTAGE );
        auto current = channel( SENSOR_CHAN_GAUGE_AVG_CURRENT );
        auto die = channel( SENSOR_CHAN_DIE_TEMP );
        auto charger = channel( static_cast<sensor_channel>( SENSOR_CHAN_NPM13XX_CHARGER_STATUS ) );
        auto vbus = channel( static_cast<sensor_channel>( SENSOR_CHAN_NPM13XX_CHARGER_VBUS_STATUS ) );
        for( const auto *result : { &voltage, &current, &die, &charger, &vbus } ) {
            if( !*result ) {
                return std::unexpected( result->error() );
            }
        }
        std::int64_t const millivolts = sensor_value_to_milli( &*voltage );
        return BatteryStatus{
            .millivolts = millivolts > 0 ? static_cast<std::uint32_t>( millivolts ) : 0U,
            .microamps = static_cast<std::int32_t>( sensor_value_to_micro( &*current ) ),
            .die_millicelsius = static_cast<std::int32_t>( sensor_value_to_milli( &*die ) ),
            .charge = charge_state( static_cast<std::uint32_t>( charger->val1 ) ),
            .external_power = ( static_cast<std::uint32_t>( vbus->val1 ) & vbus_present ) != 0U,
        };
    }

private:
    // nPM1300 BCHGCHARGESTATUS bits. VBUSINSTATUS: present.
    static constexpr std::uint32_t status_complete = 0x02U;
    static constexpr std::uint32_t status_trickle = 0x04U;
    static constexpr std::uint32_t status_constant_current = 0x08U;
    static constexpr std::uint32_t status_constant_voltage = 0x10U;
    static constexpr std::uint32_t vbus_present = 0x01U;

    [[nodiscard]] static constexpr ChargeState charge_state( std::uint32_t status ) noexcept
    {
        if( ( status & status_complete ) != 0U ) {
            return ChargeState::complete;
        }
        if( ( status & status_trickle ) != 0U ) {
            return ChargeState::trickle;
        }
        if( ( status & status_constant_current ) != 0U ) {
            return ChargeState::constant_current;
        }
        if( ( status & status_constant_voltage ) != 0U ) {
            return ChargeState::constant_voltage;
        }
        return ChargeState::idle;
    }

    [[nodiscard]] fw::Result<sensor_value> channel( sensor_channel chan ) const
    {
        sensor_value value{};
        if( auto ret = fw::err::check( sensor_channel_get( dev_, chan, &value ) ); !ret ) {
            return std::unexpected( ret.error() );
        }
        return value;
    }

    const device *dev_;
};

} // namespace hal::zephyr
