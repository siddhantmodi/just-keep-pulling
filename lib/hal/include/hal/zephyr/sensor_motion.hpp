/**
 * @file sensor_motion.hpp
 * @brief hal::zephyr::SensorMotion: hal::IMotionSensor over a Zephyr accelerometer's motion trigger.
 *
 * Runs the accelerometer at 26 Hz and sets SENSOR_TRIG_MOTION with a slope
 * threshold, so the sensor interrupts on a change of acceleration (e.g. the
 * LSM6DSL's wake-up interrupt). The trigger handler only records the motion;
 * moved() reads and clears it.
 *
 * A sensor marked zephyr,deferred-init (one powered by a regulator that
 * starts after the sensors) is initialized by init().
 * @code
 * constinit hal::zephyr::SensorMotion motion{ DEVICE_DT_GET( DT_ALIAS( motion_sensor ) ), 63 };
 * @endcode
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <expected>
#include <fw/error.hpp>
#include <hal/motion_sensor.hpp>
#include <system_error>

namespace hal::zephyr
{

class SensorMotion final : public IMotionSensor
{
public:
    /** @param threshold_milli_g The change of acceleration that counts as motion. */
    constexpr SensorMotion( const device *dev, std::int32_t threshold_milli_g ) noexcept
        : dev_{ dev }
        , threshold_milli_g_{ threshold_milli_g }
    {
    }

    [[nodiscard]] fw::Result<> init() override
    {
        if( !device_is_ready( dev_ ) ) {
            if( int const ret = device_init( dev_ ); ret != 0 && ret != -EALREADY ) {
                return std::unexpected( std::errc::no_such_device );
            }
        }
        sensor_value rate{ .val1 = sampling_hz, .val2 = 0 };
        if( auto ret = fw::err::check( sensor_attr_set( dev_, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &rate ) ); !ret ) {
            return ret;
        }
        sensor_value threshold{};
        sensor_ug_to_ms2( threshold_milli_g_ * 1000, &threshold );
        if( auto ret = fw::err::check( sensor_attr_set( dev_, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SLOPE_TH, &threshold ) ); !ret ) {
            return ret;
        }
        return fw::err::check( sensor_trigger_set( dev_, &state_.trigger, on_motion ) );
    }

    [[nodiscard]] bool moved() override
    {
        return state_.moved.exchange( false, std::memory_order_relaxed );
    }

private:
    static constexpr std::int32_t sampling_hz = 26;

    /** @brief The trigger first, so the handler can get back to the state from the trigger it is given. */
    struct State {
        sensor_trigger trigger{ .type = SENSOR_TRIG_MOTION, .chan = SENSOR_CHAN_ACCEL_XYZ };
        std::atomic<bool> moved{ false };
    };

    static void on_motion( const device * /*dev*/, const sensor_trigger *trigger )
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): State is standard-layout, trigger its first member.
        reinterpret_cast<State *>( const_cast<sensor_trigger *>( trigger ) )->moved.store( true, std::memory_order_relaxed );
    }

    const device *dev_;
    std::int32_t threshold_milli_g_;
    State state_{};
};

} // namespace hal::zephyr
