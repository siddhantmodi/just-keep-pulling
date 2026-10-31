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
        auto charger = channel( static_cast<sensor_channel>( SENSOR_CHAN_NPM13XX_CHARGER_STATUS ) );
        auto vbus = channel( static_cast<sensor_channel>( SENSOR_CHAN_NPM13XX_CHARGER_VBUS_STATUS ) );
        if( !voltage || !charger || !vbus ) {
            return std::unexpected( !voltage ? voltage.error() : !charger ? charger.error() : vbus.error() );
        }
        std::int64_t const millivolts = sensor_value_to_milli( &*voltage );
        return BatteryStatus{
            .millivolts = millivolts > 0 ? static_cast<std::uint32_t>( millivolts ) : 0U,
            .charging = ( static_cast<std::uint32_t>( charger->val1 ) & charging_mask ) != 0U,
            .external_power = ( static_cast<std::uint32_t>( vbus->val1 ) & vbus_present ) != 0U,
        };
    }

private:
    // nPM1300 BCHGCHARGESTATUS: trickle, constant current, constant voltage. VBUSINSTATUS: present.
    static constexpr std::uint32_t charging_mask = 0x04U | 0x08U | 0x10U;
    static constexpr std::uint32_t vbus_present = 0x01U;

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
