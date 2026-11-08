# SPDX-License-Identifier: Apache-2.0
"""What app_main offers beyond the Tindeq protocol, end to end: the JustKeepPulling
service (calibration, sample rate, device ID) and the standard Device Information
and Battery services, as a generic GATT client (e.g. nRF Connect) uses them.

The emulated ADS1220 reads a constant 0 codes, so calibration points all measure
the same and a second one gives an implausible factor. Fixtures (device, peer)
are in conftest.py.
"""

from __future__ import annotations

import asyncio
import re
import struct

import pytest
from bumble.att import ATT_Error, ErrorCode
from bumble.core import UUID
from bumble.gatt import (
    GATT_BATTERY_LEVEL_CHARACTERISTIC,
    GATT_CHARACTERISTIC_USER_DESCRIPTION_DESCRIPTOR,
    GATT_FIRMWARE_REVISION_STRING_CHARACTERISTIC,
    GATT_HARDWARE_REVISION_STRING_CHARACTERISTIC,
    GATT_MANUFACTURER_NAME_STRING_CHARACTERISTIC,
    GATT_MODEL_NUMBER_STRING_CHARACTERISTIC,
    Characteristic,
)
from gatt_helpers import characteristic
from zephyr_bumble import eventually, wait_for_log

SERVICE_UUID = UUID("a6bd0001-a614-4d5e-b412-e86a44627954")
CALIBRATION_UUID = UUID("a6bd0002-a614-4d5e-b412-e86a44627954")
ADD_CALIBRATION_POINT_UUID = UUID("a6bd0003-a614-4d5e-b412-e86a44627954")
RESET_CALIBRATION_UUID = UUID("a6bd0004-a614-4d5e-b412-e86a44627954")
SAMPLE_RATE_UUID = UUID("a6bd0005-a614-4d5e-b412-e86a44627954")
DEVICE_ID_UUID = UUID("a6bd0006-a614-4d5e-b412-e86a44627954")

# Calibration status (channels::CalibrationStatus).
OK = 0
MEASURING = 1
FIT_REJECTED = 3

# load_cell_svc's defaults.
DEFAULT_SAMPLE_RATE = 175

# The Battery service's level for the emulated nPM1300's 3750 mV (native_sim's CurveGauge).
EMULATED_BATTERY_PERCENT = 25


def calibration(value: bytes) -> tuple[int, int, float]:
    """(status, points, factor in g per ADC code)."""
    return struct.unpack("<BBf", value)


async def subscribe_to_calibration(peer) -> asyncio.Queue[tuple[int, int, float]]:
    notifications: asyncio.Queue[tuple[int, int, float]] = asyncio.Queue()
    await characteristic(peer, CALIBRATION_UUID).subscribe(lambda value: notifications.put_nowait(calibration(value)))
    return notifications


async def next_calibration(notifications: asyncio.Queue[tuple[int, int, float]], timeout: float = 5.0) -> tuple[int, int, float]:
    async with asyncio.timeout(timeout):
        return await notifications.get()


async def write(peer, uuid: UUID, value: bytes) -> None:
    await characteristic(peer, uuid).write_value(value, with_response=True)


async def write_error(peer, uuid: UUID, value: bytes) -> int:
    with pytest.raises(ATT_Error) as error:
        await write(peer, uuid, value)
    return error.value.error_code


@pytest.mark.asyncio
async def test_service_names_its_characteristics(peer):
    assert peer.get_services_by_uuid(SERVICE_UUID), "JustKeepPulling service not found"
    expected = {
        CALIBRATION_UUID: ("Calibration", Characteristic.Properties.READ | Characteristic.Properties.NOTIFY),
        ADD_CALIBRATION_POINT_UUID: ("Add calibration point (kg)", Characteristic.Properties.WRITE),
        RESET_CALIBRATION_UUID: ("Reset calibration", Characteristic.Properties.WRITE),
        SAMPLE_RATE_UUID: ("Sample rate (SPS)", Characteristic.Properties.READ | Characteristic.Properties.WRITE),
        DEVICE_ID_UUID: ("Device ID", Characteristic.Properties.READ),
    }
    for uuid, (name, properties) in expected.items():
        found = characteristic(peer, uuid)
        assert found.properties == properties, uuid
        descriptions = [d for d in await found.discover_descriptors() if d.type == GATT_CHARACTERISTIC_USER_DESCRIPTION_DESCRIPTOR]
        assert len(descriptions) == 1, uuid
        assert (await descriptions[0].read_value()).decode() == name


@pytest.mark.asyncio
async def test_reads_the_device_id_as_displayed(peer, device):
    assert await characteristic(peer, DEVICE_ID_UUID).read_value() == device.device_id


@pytest.mark.asyncio
async def test_calibration_starts_without_points(peer):
    status, points, factor = calibration(await characteristic(peer, CALIBRATION_UUID).read_value())
    assert (status, points) == (OK, 0)
    assert factor > 0


@pytest.mark.asyncio
async def test_calibration_points_report_their_progress(peer, ble_dut):
    notifications = await subscribe_to_calibration(peer)

    await write(peer, ADD_CALIBRATION_POINT_UUID, struct.pack("<f", 0.0))
    assert (await next_calibration(notifications))[:2] == (MEASURING, 0)
    # Averaged over 1.25 s at 90 SPS; one point does not change the factor.
    assert (await next_calibration(notifications))[:2] == (OK, 1)
    await wait_for_log(ble_dut, r"calibration point 1: ")

    # The emulated load cell reads the same under 20 kg: no factor fits.
    await write(peer, ADD_CALIBRATION_POINT_UUID, struct.pack("<f", 20.0))
    assert (await next_calibration(notifications))[:2] == (MEASURING, 1)
    assert (await next_calibration(notifications))[:2] == (FIT_REJECTED, 0)
    assert calibration(await characteristic(peer, CALIBRATION_UUID).read_value())[:2] == (FIT_REJECTED, 0)


@pytest.mark.asyncio
async def test_reset_clears_the_points(peer):
    notifications = await subscribe_to_calibration(peer)
    _, _, default_factor = calibration(await characteristic(peer, CALIBRATION_UUID).read_value())
    await write(peer, ADD_CALIBRATION_POINT_UUID, struct.pack("<f", 0.0))
    assert (await next_calibration(notifications))[:2] == (MEASURING, 0)
    assert (await next_calibration(notifications))[:2] == (OK, 1)

    await write(peer, RESET_CALIBRATION_UUID, b"\x01")
    assert await next_calibration(notifications) == (OK, 0, default_factor)


@pytest.mark.asyncio
async def test_malformed_writes_are_rejected(peer):
    assert await write_error(peer, ADD_CALIBRATION_POINT_UUID, b"\x00\x00\xa0") == ErrorCode.INVALID_ATTRIBUTE_LENGTH
    assert await write_error(peer, ADD_CALIBRATION_POINT_UUID, struct.pack("<f", -1.0)) == ErrorCode.VALUE_NOT_ALLOWED
    assert await write_error(peer, ADD_CALIBRATION_POINT_UUID, struct.pack("<f", float("nan"))) == ErrorCode.VALUE_NOT_ALLOWED
    assert await write_error(peer, RESET_CALIBRATION_UUID, b"\x00") == ErrorCode.VALUE_NOT_ALLOWED
    assert await write_error(peer, SAMPLE_RATE_UUID, struct.pack("<H", 100)) == ErrorCode.VALUE_NOT_ALLOWED
    assert await write_error(peer, SAMPLE_RATE_UUID, b"\x5a") == ErrorCode.INVALID_ATTRIBUTE_LENGTH


@pytest.mark.asyncio
async def test_sets_and_saves_the_sample_rate(peer, ble_dut):
    sample_rate = characteristic(peer, SAMPLE_RATE_UUID)

    async def read() -> int:
        return struct.unpack("<H", await sample_rate.read_value())[0]

    assert await read() == DEFAULT_SAMPLE_RATE
    await write(peer, SAMPLE_RATE_UUID, struct.pack("<H", 1000))
    await wait_for_log(ble_dut, r"sample rate set to 1000 SPS")
    # Applied by the measurement manager, so shortly after the write.
    assert await eventually(read, 1000) == 1000


@pytest.mark.asyncio
async def test_device_information_names_the_firmware(peer):
    async def read(uuid):
        return (await characteristic(peer, uuid).read_value()).decode()

    assert await read(GATT_MANUFACTURER_NAME_STRING_CHARACTERISTIC) == "JustKeepPulling"
    assert await read(GATT_MODEL_NUMBER_STRING_CHARACTERISTIC) == "JustKeepPulling"
    assert await read(GATT_HARDWARE_REVISION_STRING_CHARACTERISTIC) == "native_sim"
    assert re.fullmatch(r"\d+\.\d+\.\d+\+\d+", await read(GATT_FIRMWARE_REVISION_STRING_CHARACTERISTIC))


@pytest.mark.asyncio
async def test_battery_service_reports_the_level(peer):
    assert await characteristic(peer, GATT_BATTERY_LEVEL_CHARACTERISTIC).read_value() == bytes([EMULATED_BATTERY_PERCENT])
