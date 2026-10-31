/**
 * @file identity.hpp
 * @brief identity_svc::Identity: the device ID, generated once and kept in settings.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "identity_svc.hpp"

#include <fw/error.hpp>

namespace identity_svc
{

/** @brief Settings key of the device ID. */
inline constexpr const char *id_key = "identity/id";

class Identity final : public IIdentity
{
public:
    [[nodiscard]] fw::Result<> init() override;
    [[nodiscard]] const DeviceId &id() const override;
    [[nodiscard]] fw::Result<> set_id( const DeviceId &id ) override;

private:
    DeviceId id_{};
};

} // namespace identity_svc
