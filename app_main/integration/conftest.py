# SPDX-License-Identifier: Apache-2.0
"""Fixtures shared by the integration tests: the booted app and a connected Bumble central."""

from __future__ import annotations

import asyncio
import re
from collections.abc import AsyncIterator
from dataclasses import dataclass

import pytest_asyncio
from bumble.device import Peer
from gatt_helpers import CLIENT_MTU
from zephyr_bumble import connect_by_name, wait_for_log


@dataclass
class Device:
    """What the app logged at boot: its advertised name and device ID."""

    name: str
    device_id: bytes


@pytest_asyncio.fixture
async def device(ble_dut) -> Device:
    lines = "\n".join(await wait_for_log(ble_dut, r'advertising as "Progressor_\d{4}"'))
    name = re.search(r'advertising as "(Progressor_\d{4})"', lines)
    device_id = re.search(r"device ID ([0-9A-F]{12})", lines)
    assert name and device_id, "advertised name or device ID not found in the log"
    return Device(name=name.group(1), device_id=bytes.fromhex(device_id.group(1)))


@pytest_asyncio.fixture
async def peer(device, virtual_link) -> AsyncIterator[Peer]:
    """Connected, with the MTU exchanged and every service and characteristic discovered."""
    connection = await connect_by_name(virtual_link.central, device.name)
    disconnected = asyncio.Event()
    connection.on("disconnection", lambda reason: disconnected.set())
    peer = Peer(connection)
    await peer.request_mtu(CLIENT_MTU)
    await peer.discover_services()
    for service in peer.services:
        await peer.discover_characteristics(service=service)
    yield peer
    if not disconnected.is_set():
        await connection.disconnect()
