# SPDX-License-Identifier: Apache-2.0
"""Test helpers: find and connect to the app, and wait on its output or state."""

from __future__ import annotations

import asyncio
import re
from collections.abc import Awaitable, Callable
from typing import TypeVar

from bumble.core import UUID, AdvertisingData
from bumble.device import Advertisement, Connection, Device

T = TypeVar("T")


_SERVICE_UUID_LISTS = (
    AdvertisingData.COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS,
    AdvertisingData.INCOMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS,
    AdvertisingData.COMPLETE_LIST_OF_32_BIT_SERVICE_CLASS_UUIDS,
    AdvertisingData.INCOMPLETE_LIST_OF_32_BIT_SERVICE_CLASS_UUIDS,
    AdvertisingData.COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS,
    AdvertisingData.INCOMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS,
)


def _advertises(advertisement: Advertisement, name: str | None, service_uuid: UUID | None) -> bool:
    data = advertisement.data
    if name is not None and data.get(AdvertisingData.COMPLETE_LOCAL_NAME) != name:
        return False
    if service_uuid is not None:
        uuids = [uuid for ad_type in _SERVICE_UUID_LISTS for uuid in (data.get(ad_type) or [])]
        if service_uuid not in uuids:
            return False
    return True


async def find_advertiser(
    central: Device,
    name: str | None = None,
    *,
    service_uuid: UUID | None = None,
    timeout: float = 15.0,
) -> Advertisement:
    """Scan until a device advertises @name (complete local name) and/or @service_uuid.

    Only the advertising data is checked: Bumble's virtual controller does not
    deliver scan responses, so match on something the app puts in its
    advertising data. Raises TimeoutError if no device matches within
    @timeout, which also makes this the way to assert that a device has
    stopped advertising.
    """
    if name is None and service_uuid is None:
        raise ValueError("find_advertiser needs a name, a service_uuid, or both")

    wanted = " and ".join(
        part for part in (name and f"name {name!r}", service_uuid and f"service {service_uuid}") if part
    )
    found: asyncio.Future[Advertisement] = asyncio.get_running_loop().create_future()

    def on_advertisement(advertisement: Advertisement) -> None:
        if not found.done() and _advertises(advertisement, name, service_uuid):
            found.set_result(advertisement)

    central.on("advertisement", on_advertisement)
    await central.start_scanning()
    try:
        async with asyncio.timeout(timeout):
            return await found
    except TimeoutError as exc:
        raise TimeoutError(f"no device advertising {wanted} within {timeout}s") from exc
    finally:
        central.remove_listener("advertisement", on_advertisement)
        await central.stop_scanning()


async def _connect(central: Device, advertisement: Advertisement, timeout: float) -> Connection:
    async with asyncio.timeout(timeout):
        return await central.connect(advertisement.address)


async def connect_by_name(central: Device, name: str, timeout: float = 15.0) -> Connection:
    """Scan for a device advertising @name as its complete local name and connect to it."""
    return await _connect(central, await find_advertiser(central, name, timeout=timeout), timeout)


async def connect_by_service(central: Device, service_uuid: UUID, timeout: float = 15.0) -> Connection:
    """Scan for a device advertising @service_uuid and connect to it."""
    advertisement = await find_advertiser(central, service_uuid=service_uuid, timeout=timeout)
    return await _connect(central, advertisement, timeout)


async def wait_for_log(dut, regex: str, timeout: float = 10.0) -> list[str]:
    """Wait for a line of app output matching @regex.

    Twister's DeviceAdapter reads output on a thread; waiting in a worker
    thread keeps Bumble's event loop running meanwhile.
    """
    re.compile(regex)  # Fail fast on a bad pattern, not after the timeout.
    return await asyncio.to_thread(dut.readlines_until, regex=regex, timeout=timeout)


async def eventually(read: Callable[[], Awaitable[T]], expected: T, timeout: float = 5.0, interval: float = 0.05) -> T:
    """Call @read until it returns @expected, e.g. for state the app updates asynchronously."""
    value: T | None = None
    try:
        async with asyncio.timeout(timeout):
            while (value := await read()) != expected:
                await asyncio.sleep(interval)
    except TimeoutError as exc:
        raise AssertionError(f"expected {expected!r} within {timeout}s, last read {value!r}") from exc
    return value
