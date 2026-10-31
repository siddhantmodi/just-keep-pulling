# SPDX-License-Identifier: Apache-2.0
"""The virtual radio: two Bumble controllers on a LocalLink, one for the Zephyr app and one for a Bumble central."""

from __future__ import annotations

import socket

from bumble.controller import Controller
from bumble.device import Device
from bumble.host import Host
from bumble.link import LocalLink
from bumble.transport.common import AsyncPipeSink, Transport
from bumble.transport.tcp_server import open_tcp_server_transport_with_socket


class VirtualLink:
    """A virtual BLE radio shared by a Zephyr app (over TCP HCI) and a Bumble central.

    The app's controller has no public address, so Zephyr creates a static
    random identity, as on Nordic hardware. Bumble's LocalLink routes LE
    traffic by the sender's random address, so this also needs no address
    workaround.
    """

    def __init__(self, transport: Transport, port: int, central: Device) -> None:
        self._transport = transport
        self.port = port
        self.central = central

    @classmethod
    async def open(cls, central_name: str = "zephyr-bumble central") -> VirtualLink:
        link = LocalLink()

        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.bind(("127.0.0.1", 0))
        sock.listen(1)
        port = sock.getsockname()[1]
        transport = await open_tcp_server_transport_with_socket(sock)
        # Kept alive by the link; Zephyr connects to it as its HCI controller.
        Controller("dut", host_source=transport.source, host_sink=transport.sink, link=link)

        central_controller = Controller("central", link=link)
        central = Device(name=central_name, host=Host(central_controller, AsyncPipeSink(central_controller)))
        await central.power_on()
        return cls(transport, port, central)

    @property
    def bt_dev_arg(self) -> str:
        """The native_sim command-line option that connects the app to this link."""
        return f"--bt-dev=127.0.0.1:{self.port}"

    async def close(self) -> None:
        await self._transport.close()
