/**
 * @file nrf_fuel_gauge.hpp
 * @brief hal::zephyr::NrfFuelGauge: hal::IFuelGauge over Nordic's nRF Fuel Gauge library (nrfxlib, secondary cell).
 *
 * Constructed from a battery model (made with Nordic's nPM PowerUP) and the
 * charger's settings, which the library needs for time-to-full:
 * @code
 * constinit hal::zephyr::NrfFuelGauge gauge{ model, 500'000, 50'000 };
 * @endcode
 *
 * The library keeps one state for the whole program, so there is at most one
 * instance. Not thread-safe: call it from a single thread. The library is
 * built for Arm and Xtensa only, not native_sim.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <fw/error.hpp>
#include <hal/fuel_gauge.hpp>
#include <nrf_fuel_gauge.h>
#include <span>
#include <system_error>

namespace hal::zephyr
{

class NrfFuelGauge final : public IFuelGauge
{
public:
    constexpr NrfFuelGauge( const battery_model &model, std::uint32_t charge_microamps, std::uint32_t term_microamps ) noexcept
        : model_{ model }
        , charge_microamps_{ charge_microamps }
        , term_microamps_{ term_microamps }
    {
    }

    [[nodiscard]] fw::Result<> init( const BatteryStatus &status, std::span<const std::byte> saved_state ) override
    {
        if( !saved_state.empty() && nrf_fuel_gauge_state_compatible_check( saved_state.data(), saved_state.size() ) != 0 ) {
            return std::unexpected( std::errc::invalid_argument );
        }
        nrf_fuel_gauge_init_parameters parameters{};
        parameters.v0 = volts( status );
        parameters.i0 = discharge_amps( status );
        parameters.t0 = celsius( status );
        parameters.model = &model_;
        parameters.state = saved_state.empty() ? nullptr : saved_state.data();
        parameters.state_size = saved_state.size();
        if( auto ret = fw::err::check( nrf_fuel_gauge_init( &parameters, nullptr ) ); !ret ) {
            return ret;
        }
        nrf_fuel_gauge_ext_state_info_data limit{};
        limit.charge_current_limit = static_cast<float>( charge_microamps_ ) / 1e6F;
        if( auto ret = fw::err::check( nrf_fuel_gauge_ext_state_update( NRF_FUEL_GAUGE_EXT_STATE_INFO_CHARGE_CURRENT_LIMIT, &limit ) ); !ret ) {
            return ret;
        }
        nrf_fuel_gauge_ext_state_info_data term{};
        term.charge_term_current = static_cast<float>( term_microamps_ ) / 1e6F;
        if( auto ret = fw::err::check( nrf_fuel_gauge_ext_state_update( NRF_FUEL_GAUGE_EXT_STATE_INFO_TERM_CURRENT, &term ) ); !ret ) {
            return ret;
        }
        if( auto ret = inform_charge( status.charge ); !ret ) {
            return ret;
        }
        charge_ = status.charge;
        return {};
    }

    [[nodiscard]] fw::Result<float> update( const BatteryStatus &status, std::chrono::milliseconds elapsed ) override
    {
        auto const vbus = status.external_power ? NRF_FUEL_GAUGE_EXT_STATE_INFO_VBUS_CONNECTED : NRF_FUEL_GAUGE_EXT_STATE_INFO_VBUS_DISCONNECTED;
        if( auto ret = fw::err::check( nrf_fuel_gauge_ext_state_update( vbus, nullptr ) ); !ret ) {
            return std::unexpected( ret.error() );
        }
        if( status.charge != charge_ ) {
            if( auto ret = inform_charge( status.charge ); !ret ) {
                return std::unexpected( ret.error() );
            }
            charge_ = status.charge;
        }
        float soc = 0.0F;
        if( elapsed.count() == 0 ) {
            if( auto ret = fw::err::check( nrf_fuel_gauge_soc_get( &soc ) ); !ret ) {
                return std::unexpected( ret.error() );
            }
            return soc;
        }
        float const seconds = static_cast<float>( elapsed.count() ) / 1000.0F;
        if( auto ret =
                fw::err::check( nrf_fuel_gauge_process( volts( status ), discharge_amps( status ), celsius( status ), seconds, &soc, nullptr ) );
            !ret ) {
            return std::unexpected( ret.error() );
        }
        return soc;
    }

    [[nodiscard]] std::size_t state_size() const override
    {
        return nrf_fuel_gauge_state_size;
    }

    [[nodiscard]] fw::Result<> save_state( std::span<std::byte> out ) override
    {
        return fw::err::check( nrf_fuel_gauge_state_get( out.data(), out.size() ) );
    }

private:
    [[nodiscard]] static float volts( const BatteryStatus &status ) noexcept
    {
        return static_cast<float>( status.millivolts ) / 1000.0F;
    }

    /** @brief The library's sign is the opposite of Zephyr's: positive while discharging. */
    [[nodiscard]] static float discharge_amps( const BatteryStatus &status ) noexcept
    {
        return -static_cast<float>( status.microamps ) / 1e6F;
    }

    [[nodiscard]] static float celsius( const BatteryStatus &status ) noexcept
    {
        return static_cast<float>( status.die_millicelsius ) / 1000.0F;
    }

    static fw::Result<> inform_charge( ChargeState charge )
    {
        nrf_fuel_gauge_ext_state_info_data data{};
        switch( charge ) {
        case ChargeState::complete:
            data.charge_state = NRF_FUEL_GAUGE_CHARGE_STATE_COMPLETE;
            break;
        case ChargeState::trickle:
            data.charge_state = NRF_FUEL_GAUGE_CHARGE_STATE_TRICKLE;
            break;
        case ChargeState::constant_current:
            // As Nordic's sample: the USB current limit may hold the charge current below its setting.
            data.charge_state = NRF_FUEL_GAUGE_CHARGE_STATE_CC_LIMITED;
            break;
        case ChargeState::constant_voltage:
            data.charge_state = NRF_FUEL_GAUGE_CHARGE_STATE_CV;
            break;
        case ChargeState::idle:
            data.charge_state = NRF_FUEL_GAUGE_CHARGE_STATE_IDLE;
            break;
        }
        return fw::err::check( nrf_fuel_gauge_ext_state_update( NRF_FUEL_GAUGE_EXT_STATE_INFO_CHARGE_STATE_CHANGE, &data ) );
    }

    const battery_model &model_;
    std::uint32_t charge_microamps_;
    std::uint32_t term_microamps_;
    ChargeState charge_{ ChargeState::idle };
};

} // namespace hal::zephyr
