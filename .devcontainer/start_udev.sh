#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Let non-root tools (nRF Util, J-Link) open debug probes. Probes are raw USB
# devices under /dev/bus/usb, which stay root:root 0660 without a udev rule;
# serial ports need none (group dialout). OrbStack shares one Linux instance,
# and its /dev, between containers and runs no udev of its own, but kernel
# device events reach this container, so udevd here applies SEGGER's rules to
# probes present now and to every later replug.

set -euo pipefail

rules=/opt/SEGGER/JLink/99-jlink.rules
if [ ! -f "$rules" ]; then
    echo "start_udev.sh: $rules not found, skipping" >&2
    exit 0
fi

sudo mkdir -p /etc/udev/rules.d
sudo cp "$rules" /etc/udev/rules.d/
if ! pgrep -x systemd-udevd >/dev/null; then
    sudo /lib/systemd/systemd-udevd --daemon
fi
sudo udevadm trigger --subsystem-match=usb --action=add
sudo udevadm settle --timeout=10
