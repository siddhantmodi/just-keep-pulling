/**
 * @file motion_svc.hpp
 * @brief Motion service public API: the IMotion interface and the app's IMU.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fw/error.hpp>

namespace motion_svc
{

class IMotion
{
public:
    /** @brief Start detecting motion. Fails with no_such_device if the IMU or its wake pin is not usable. */
    [[nodiscard]] virtual fw::Result<> init() = 0;

    /** @brief Whether the device moved since init() or the last call. Thread-safe. */
    [[nodiscard]] virtual bool moved() = 0;

    /**
     * @brief Power the SoC off (System OFF) until the device moves; it then
     *        starts again from reset. Returns only if that fails, e.g. with
     *        not_supported on native_sim.
     */
    [[nodiscard]] virtual fw::Result<> sleep_until_moved() = 0;

protected:
    // Never deleted through the interface: objects are static.
    ~IMotion() = default;
};

/** @brief The app's IMU (devicetree alias motion-sensor; its irq-gpios wakes the SoC). */
[[nodiscard]] IMotion &motion();

} // namespace motion_svc
