# SPDX-License-Identifier: Apache-2.0
"""app_main as a Tindeq Progressor, end to end: a Bumble central does what the Tindeq app does."""

from __future__ import annotations

import re
from collections.abc import AsyncIterator

import pytest
import pytest_asyncio
from bumble.core import UUID
from bumble.device import Peer
from bumble.gatt import GATT_DEVICE_NAME_CHARACTERISTIC, Characteristic
from zephyr_bumble import connect_by_name, wait_for_log

SERVICE_UUID = UUID("7e4e1701-1ea6-40c9-9dcc-13d34ffead57")
DATA_POINT_UUID = UUID("7e4e1702-1ea6-40c9-9dcc-13d34ffead57")
CONTROL_POINT_UUID = UUID("7e4e1703-1ea6-40c9-9dcc-13d34ffead57")


@pytest_asyncio.fixture
async def advertised_name(ble_dut) -> str:
    """The name the app advertises, which comes from its random device ID."""
    lines = await wait_for_log(ble_dut, r'advertising as "Progressor_\d{4}"')
    match = re.search(r'advertising as "(Progressor_\d{4})"', "\n".join(lines))
    assert match, "advertised name not found in the log"
    return match.group(1)


@pytest_asyncio.fixture
async def peer(advertised_name, virtual_link) -> AsyncIterator[Peer]:
    connection = await connect_by_name(virtual_link.central, advertised_name)
    peer = Peer(connection)
    await peer.discover_services()
    for service in peer.services:
        await peer.discover_characteristics(service=service)
    yield peer
    await connection.disconnect()


def characteristic(peer: Peer, uuid: UUID):
    found = peer.get_characteristics_by_uuid(uuid)
    assert len(found) == 1, f"expected one characteristic {uuid}, found {len(found)}"
    return found[0]


@pytest.mark.asyncio
async def test_device_name_matches_advertised_name(peer, advertised_name):
    # Bumble's virtual controller reports the advertising data again as the scan
    # response, so the service UUID in the real scan response is checked on hardware.
    device_name = peer.get_characteristics_by_uuid(GATT_DEVICE_NAME_CHARACTERISTIC)
    assert len(device_name) == 1
    assert (await device_name[0].read_value()).decode() == advertised_name


@pytest.mark.asyncio
async def test_service_has_data_and_control_points(peer):
    assert peer.get_services_by_uuid(SERVICE_UUID), "Progressor service not found"

    data_point = characteristic(peer, DATA_POINT_UUID)
    control_point = characteristic(peer, CONTROL_POINT_UUID)

    assert data_point.properties & Characteristic.Properties.NOTIFY
    assert control_point.properties & Characteristic.Properties.WRITE
    assert control_point.properties & Characteristic.Properties.WRITE_WITHOUT_RESPONSE


@pytest.mark.asyncio
async def test_unknown_opcode_is_logged_and_ignored(peer, ble_dut):
    # 0x67 is the undocumented peak-RFD command, which this device does not implement.
    await characteristic(peer, CONTROL_POINT_UUID).write_value(b"\x67", with_response=True)
    await wait_for_log(ble_dut, r"unsupported opcode 0x67 ignored")

