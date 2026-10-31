# SPDX-License-Identifier: Apache-2.0
"""pytest plugin: the virtual_link and ble_dut fixtures. Builds on Twister's pytest-twister-harness."""

from __future__ import annotations

import asyncio
from collections.abc import AsyncIterator

import pytest
import pytest_asyncio

from zephyr_bumble.link import VirtualLink


@pytest_asyncio.fixture
async def virtual_link() -> AsyncIterator[VirtualLink]:
    """A virtual radio with a powered-on Bumble central, ready for the app to connect to."""
    link = await VirtualLink.open()
    try:
        yield link
    finally:
        await link.close()


@pytest_asyncio.fixture
async def ble_dut(virtual_link: VirtualLink, unlaunched_dut):
    """Twister's device under test, launched as zephyr.exe --bt-dev=<virtual_link>.

    Depends on virtual_link first, so the app is stopped before the link is
    closed. Twister's adapter only adds its own extra arguments when it builds the
    command itself, so build it here: the native_sim command, Twister's extra
    arguments, then --bt-dev. Launching starts a process and reader threads,
    so it runs off the event loop.
    """
    if unlaunched_dut.device_config.type != "native":
        pytest.skip("zephyr_bumble's virtual link needs a native_sim build")

    unlaunched_dut.generate_command()
    unlaunched_dut.command.extend(unlaunched_dut.device_config.extra_test_args.split())
    unlaunched_dut.command.append(virtual_link.bt_dev_arg)
    await asyncio.to_thread(unlaunched_dut.launch)
    yield unlaunched_dut
