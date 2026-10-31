# SPDX-License-Identifier: Apache-2.0
"""Test helpers: find and connect to the app, and wait on its output or state."""

from __future__ import annotations

import asyncio
import re
from collections.abc import Awaitable, Callable
from typing import TypeVar

from bumble.core import AdvertisingData
from bumble.device import Advertisement, Connection, Device

T = TypeVar("T")


async def connect_by_name(central: Device, name: str, timeout: float = 15.0) -> Connection:
    """Scan for a device advertising @name as its complete local name and connect to it."""
    found: asyncio.Future[Advertisement] = asyncio.get_running_loop().create_future()

    def on_advertisement(advertisement: Advertisement) -> None:
        advertised = advertisement.data.get(AdvertisingData.COMPLETE_LOCAL_NAME)
        if not found.done() and advertised == name:
            found.set_result(advertisement)

    central.on("advertisement", on_advertisement)
    await central.start_scanning()
    try:
        async with asyncio.timeout(timeout):
            advertisement = await found
    except TimeoutError as exc:
        raise TimeoutError(f"no device advertising {name!r} within {timeout}s") from exc
    finally:
        central.remove_listener("advertisement", on_advertisement)
        await central.stop_scanning()

    async with asyncio.timeout(timeout):
        return await central.connect(advertisement.address)


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
