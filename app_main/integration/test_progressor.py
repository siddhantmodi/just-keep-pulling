# SPDX-License-Identifier: Apache-2.0
"""app_main as a Tindeq Progressor, end to end: a Bumble central does what the Tindeq app does.

The emulated ADS1220 reads a constant 0 codes, which the boot-time tare zeroes,
so streamed weights are 0 kg; the maths is unit tested. The emulated nPM1300's
battery reads 3750 mV, so the low-battery power-off is unit tested too.
Fixtures (device, peer) are in conftest.py.
"""

from __future__ import annotations

import asyncio
import struct
from dataclasses import dataclass

import pytest
import pytest_asyncio
from bumble.core import UUID
from bumble.device import Connection, Peer
from bumble.gatt import GATT_DEVICE_NAME_CHARACTERISTIC, Characteristic
from gatt_helpers import characteristic
from zephyr_bumble import connect_by_name, wait_for_log

SERVICE_UUID = UUID("7e4e1701-1ea6-40c9-9dcc-13d34ffead57")
DATA_POINT_UUID = UUID("7e4e1702-1ea6-40c9-9dcc-13d34ffead57")
CONTROL_POINT_UUID = UUID("7e4e1703-1ea6-40c9-9dcc-13d34ffead57")

TARE = b"\x64"
START_MEASUREMENT = b"\x65"
STOP_MEASUREMENT = b"\x66"
GET_APP_VERSION = b"\x6b"
SHUTDOWN = b"\x6e"
SAMPLE_BATTERY = b"\x6f"
GET_PROGRESSOR_ID = b"\x70"

COMMAND_RESPONSE = 0x00
WEIGHT_MEASUREMENT = 0x01

# The emulated nPM1300's battery (app_main/boards/native_sim.overlay), as the power manager last read it.
EMULATED_BATTERY_MV = 3750

# 175 SPS, the default rate: 8 samples in 50 ms.
SAMPLE_PERIOD_US = 1_000_000 // 175
SAMPLES_PER_BATCH = 8


@dataclass
class Client:
    """A connected client subscribed to the data point, as the Tindeq app is."""

    connection: Connection
    peer: Peer
    control_point: object
    notifications: asyncio.Queue[bytes]

    async def write(self, command: bytes) -> None:
        await self.control_point.write_value(command, with_response=True)

    async def next_notification(self, timeout: float = 5.0) -> bytes:
        async with asyncio.timeout(timeout):
            return await self.notifications.get()

    async def command_response(self, command: bytes) -> bytes:
        await self.write(command)
        response = await self.next_notification()
        assert response[0] == COMMAND_RESPONSE
        assert response[1] == len(response) - 2
        return response[2:]


@pytest_asyncio.fixture
async def client(peer) -> Client:
    notifications: asyncio.Queue[bytes] = asyncio.Queue()
    await characteristic(peer, DATA_POINT_UUID).subscribe(notifications.put_nowait)
    return Client(
        connection=peer.connection,
        peer=peer,
        control_point=characteristic(peer, CONTROL_POINT_UUID),
        notifications=notifications,
    )


def weight_samples(notification: bytes) -> list[tuple[float, int]]:
    assert notification[0] == WEIGHT_MEASUREMENT
    assert notification[1] == len(notification) - 2
    assert notification[1] % 8 == 0
    return list(struct.iter_unpack("<fI", notification[2:]))


async def stream(client: Client, count: int) -> list[tuple[float, int]]:
    """Collect at least @count samples from the weight measurement notifications."""
    samples: list[tuple[float, int]] = []
    while len(samples) < count:
        samples += weight_samples(await client.next_notification())
    return samples


async def drain(client: Client, quiet: float = 0.5) -> list[bytes]:
    """Collect notifications until none arrives for @quiet seconds."""
    drained = []
    while True:
        try:
            drained.append(await client.next_notification(timeout=quiet))
        except TimeoutError:
            return drained


@pytest.mark.asyncio
async def test_device_name_matches_advertised_name(peer, device):
    # Bumble's virtual controller reports the advertising data again as the scan
    # response, so the service UUID in the real scan response is checked on hardware.
    device_name = peer.get_characteristics_by_uuid(GATT_DEVICE_NAME_CHARACTERISTIC)
    assert len(device_name) == 1
    assert (await device_name[0].read_value()).decode() == device.name


@pytest.mark.asyncio
async def test_service_has_data_and_control_points(peer):
    assert peer.get_services_by_uuid(SERVICE_UUID), "Progressor service not found"

    data_point = characteristic(peer, DATA_POINT_UUID)
    control_point = characteristic(peer, CONTROL_POINT_UUID)

    assert data_point.properties & Characteristic.Properties.NOTIFY
    assert control_point.properties & Characteristic.Properties.WRITE
    assert control_point.properties & Characteristic.Properties.WRITE_WITHOUT_RESPONSE


@pytest.mark.asyncio
async def test_unknown_opcode_is_logged_and_ignored(client, ble_dut):
    # 0x67 is the undocumented peak-RFD command, which this device does not implement.
    await client.write(b"\x67")
    await wait_for_log(ble_dut, r"unsupported opcode 0x67 ignored")
    assert await drain(client) == []


@pytest.mark.asyncio
async def test_answers_app_version(client):
    assert await client.command_response(GET_APP_VERSION) == b"2.0.4"


@pytest.mark.asyncio
async def test_answers_progressor_id_least_significant_byte_first(client, device):
    assert await client.command_response(GET_PROGRESSOR_ID) == device.device_id[::-1]


@pytest.mark.asyncio
async def test_answers_battery_voltage(client):
    millivolts = struct.unpack("<I", await client.command_response(SAMPLE_BATTERY))[0]
    assert millivolts == EMULATED_BATTERY_MV


@pytest.mark.asyncio
async def test_tare_and_measure(client):
    await client.write(TARE)
    await client.write(START_MEASUREMENT)

    first = weight_samples(await client.next_notification())
    assert len(first) == SAMPLES_PER_BATCH
    samples = first + await stream(client, 3 * SAMPLES_PER_BATCH)

    assert all(kg == 0.0 for kg, _ in samples)
    timestamps = [timestamp for _, timestamp in samples]
    assert timestamps[0] == SAMPLE_PERIOD_US
    # Taken in the ADC's data-ready interrupt: one period apart, to the emulator's 10 us tick.
    assert all(abs(b - a - SAMPLE_PERIOD_US) <= 10 for a, b in zip(timestamps, timestamps[1:]))

    await client.write(STOP_MEASUREMENT)
    # The pending samples arrive, then nothing more.
    for notification in await drain(client):
        assert len(weight_samples(notification)) <= SAMPLES_PER_BATCH


@pytest.mark.asyncio
async def test_a_new_measurement_starts_a_new_timeline(client):
    await client.write(START_MEASUREMENT)
    await stream(client, 2 * SAMPLES_PER_BATCH)
    await client.write(STOP_MEASUREMENT)
    await drain(client)

    await client.write(START_MEASUREMENT)
    samples = weight_samples(await client.next_notification())
    assert samples[0][1] == SAMPLE_PERIOD_US
    await client.write(STOP_MEASUREMENT)
    await drain(client)


@pytest.mark.asyncio
async def test_commands_are_answered_while_measuring(client):
    await client.write(START_MEASUREMENT)
    await stream(client, SAMPLES_PER_BATCH)

    await client.write(GET_APP_VERSION)
    async with asyncio.timeout(5):
        while (notification := await client.next_notification())[0] != COMMAND_RESPONSE:
            weight_samples(notification)
    assert notification[2:] == b"2.0.4"

    await client.write(STOP_MEASUREMENT)
    await drain(client)


@pytest.mark.asyncio
async def test_calibration_is_not_on_the_progressor_service(client, ble_dut):
    # 0x69 is a Progressor command (Crimpdeq used it for calibration points);
    # calibration is on the JustKeepPulling service (test_justkeeppulling.py).
    await client.write(b"\x69" + struct.pack("<f", 20.0))
    await wait_for_log(ble_dut, r"unsupported opcode 0x69 ignored")
    assert await drain(client) == []


@pytest.mark.asyncio
async def test_disconnecting_stops_the_measurement(client, ble_dut):
    await client.write(START_MEASUREMENT)
    await stream(client, SAMPLES_PER_BATCH)

    await client.connection.disconnect()
    await wait_for_log(ble_dut, r"client disconnected, measurement stopped")


@pytest.mark.asyncio
async def test_shutdown_disconnects_and_advertising_resumes(client, ble_dut, device, virtual_link):
    disconnected = asyncio.get_running_loop().create_future()
    client.connection.on("disconnection", lambda reason: disconnected.set_result(reason))

    await client.write(SHUTDOWN)
    async with asyncio.timeout(5):
        await disconnected
    await wait_for_log(ble_dut, r"shutdown requested")

    # As Crimpdeq: the device stays on and can be found again. Wait for it to
    # report the connection: dropping the link while it is still being set up
    # is a different case (a connection the device never announces).
    connection = await connect_by_name(virtual_link.central, device.name)
    await wait_for_log(ble_dut, r"ble_svc: connected")
    await connection.disconnect()


@pytest.mark.asyncio
async def test_a_link_dropped_at_once_leaves_the_device_usable(device, ble_dut, virtual_link):
    # Dropped before the device may have set it up (it then never announces it).
    connection = await connect_by_name(virtual_link.central, device.name)
    await connection.disconnect()

    connection = await connect_by_name(virtual_link.central, device.name)
    peer = Peer(connection)
    await peer.discover_services()
    for service in peer.services:
        await peer.discover_characteristics(service=service)
    notifications: asyncio.Queue[bytes] = asyncio.Queue()
    await characteristic(peer, DATA_POINT_UUID).subscribe(notifications.put_nowait)
    await characteristic(peer, CONTROL_POINT_UUID).write_value(GET_APP_VERSION, with_response=True)
    async with asyncio.timeout(5):
        assert (await notifications.get())[2:] == b"2.0.4"
    await connection.disconnect()
