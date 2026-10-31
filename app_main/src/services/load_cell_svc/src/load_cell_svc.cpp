/**
 * @file load_cell_svc.cpp
 * @brief The app's load cell: load_cell_svc::LoadCell on the devicetree load-cell-adc ADS1220.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "load_cell_svc.hpp"

#include "load_cell.hpp"

#include <zephyr/device.h>

#include <hal/zephyr/ads1220.hpp>

namespace
{

constinit hal::zephyr::Ads1220 adc{ DEVICE_DT_GET( DT_ALIAS( load_cell_adc ) ) };
constinit load_cell_svc::LoadCell instance{ adc };

} // namespace

load_cell_svc::ILoadCell &load_cell_svc::load_cell()
{
    return instance;
}
