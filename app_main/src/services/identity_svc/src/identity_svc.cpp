/**
 * @file identity_svc.cpp
 * @brief The app's identity instance.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "identity_svc.hpp"

#include "identity.hpp"

namespace
{

constinit identity_svc::Identity instance;

} // namespace

identity_svc::IIdentity &identity_svc::identity()
{
    return instance;
}
