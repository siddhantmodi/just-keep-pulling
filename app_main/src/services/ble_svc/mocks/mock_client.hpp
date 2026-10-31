/**
 * @file mock_client.hpp
 * @brief ble_svc::MockClient: GoogleMock of ble_svc::IClient, for tests of its users.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "ble_svc.hpp"

#include <cstdint>
#include <fw/error.hpp>
#include <gmock/gmock.h>
#include <span>

namespace ble_svc
{

class MockClient : public IClient
{
public:
    MOCK_METHOD( fw::Result<>, send, ( std::span<const std::uint8_t> packet ), ( override ) );
    MOCK_METHOD( std::uint16_t, att_mtu, (), ( const, override ) );
    MOCK_METHOD( fw::Result<>, set_streaming, ( bool streaming ), ( override ) );
    MOCK_METHOD( fw::Result<>, disconnect, (), ( override ) );
};

} // namespace ble_svc
