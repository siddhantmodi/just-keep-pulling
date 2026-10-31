# SPDX-License-Identifier: Apache-2.0
"""Helpers shared by the integration tests (the fixtures are in conftest.py)."""

from __future__ import annotations

from bumble.core import UUID
from bumble.device import Peer

# The ATT MTU iOS negotiates, which carries 30 samples per notification.
CLIENT_MTU = 247


def characteristic(peer: Peer, uuid: UUID):
    found = peer.get_characteristics_by_uuid(uuid)
    assert len(found) == 1, f"expected one characteristic {uuid}, found {len(found)}"
    return found[0]
