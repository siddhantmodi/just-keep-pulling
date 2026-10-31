# SPDX-License-Identifier: Apache-2.0
"""app_main's GATT service, end to end: BLE central -> ble_svc -> app manager -> led_svc / temp_svc."""

from __future__ import annotations

from collections.abc import AsyncIterator

import pytest
import pytest_asyncio
from bumble.core import UUID, ProtocolError
from bumble.device import Peer
from bumble.gatt import Characteristic
from zephyr_bumble import connect_by_name, eventually, wait_for_log

DEVICE_NAME = "Just Keep Pulling"
SERVICE_UUID = UUID("9bb40001-b8c3-4eba-a5ac-7924595b107a")
LED_UUID = UUID("9bb40002-b8c3-4eba-a5ac-7924595b107a")
TEMPERATURE_UUID = UUID("9bb40003-b8c3-4eba-a5ac-7924595b107a")

# app_main/boards/native_sim.overlay sets the emulated sensor to 25370 milli-degrees C.
EMULATED_CENTICELSIUS = 2537

ATT_ERROR_INVALID_ATTRIBUTE_LENGTH = 0x0D
ATT_ERROR_VALUE_NOT_ALLOWED = 0x13


@pytest_asyncio.fixture
async def peer(ble_dut, virtual_link) -> AsyncIterator[Peer]:
    connection = await connect_by_name(virtual_link.central, DEVICE_NAME)
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
async def test_service_exposes_led_and_temperature(peer):
    assert peer.get_services_by_uuid(SERVICE_UUID), "custom service not found"

    led = characteristic(peer, LED_UUID)
    temperature = characteristic(peer, TEMPERATURE_UUID)

    assert led.properties & Characteristic.Properties.READ
    assert led.properties & Characteristic.Properties.WRITE
    assert temperature.properties & Characteristic.Properties.READ
    assert not temperature.properties & Characteristic.Properties.WRITE


@pytest.mark.asyncio
async def test_led_starts_off(peer):
    assert await characteristic(peer, LED_UUID).read_value() == b"\x00"


@pytest.mark.asyncio
async def test_led_write_turns_led_on_and_off(peer, ble_dut):
    led = characteristic(peer, LED_UUID)

    await led.write_value(b"\x01", with_response=True)
    await wait_for_log(ble_dut, r"led_svc: LED on")
    # The write is acknowledged once accepted; the manager publishes the new state shortly after.
    await eventually(led.read_value, b"\x01")

    await led.write_value(b"\x00", with_response=True)
    await wait_for_log(ble_dut, r"led_svc: LED off")
    await eventually(led.read_value, b"\x00")


@pytest.mark.asyncio
@pytest.mark.parametrize(
    ("value", "error"),
    [
        (b"\x02", ATT_ERROR_VALUE_NOT_ALLOWED),
        (b"\x01\x00", ATT_ERROR_INVALID_ATTRIBUTE_LENGTH),
        (b"", ATT_ERROR_INVALID_ATTRIBUTE_LENGTH),
    ],
    ids=["value-2", "two-bytes", "empty"],
)
async def test_led_rejects_invalid_writes(peer, value, error):
    led = characteristic(peer, LED_UUID)

    with pytest.raises(ProtocolError) as excinfo:
        await led.write_value(value, with_response=True)

    assert excinfo.value.error_code == error
    assert await led.read_value() == b"\x00"


@pytest.mark.asyncio
async def test_temperature_reads_emulated_sensor(peer):
    value = await characteristic(peer, TEMPERATURE_UUID).read_value()

    assert len(value) == 2
    assert int.from_bytes(value, "little", signed=True) == EMULATED_CENTICELSIUS
