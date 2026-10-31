/**
 * @file pmic_svc.cpp
 * @brief The app's PMIC: the nPM1300's charger and regulators, bound by devicetree alias.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pmic_svc.hpp"

#include "pmic.hpp"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#include <hal/zephyr/npm13xx_charger.hpp>
#include <hal/zephyr/regulator_ship_mode.hpp>

namespace
{

constinit hal::zephyr::Npm13xxCharger charger{ DEVICE_DT_GET( DT_ALIAS( pmic_charger ) ) };
constinit hal::zephyr::RegulatorShipMode ship_mode{ DEVICE_DT_GET( DT_ALIAS( pmic_regulators ) ) };
constinit pmic_svc::Pmic instance{ charger, ship_mode };

} // namespace

namespace pmic_svc
{

IPmic &pmic()
{
    return instance;
}

} // namespace pmic_svc
