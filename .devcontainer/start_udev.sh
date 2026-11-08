#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Let non-root tools open debug probes and the Bluetooth HCI dongle: raw USB
# devices under /dev/bus/usb stay root:root 0660 without a udev rule (serial
# ports need none). OrbStack runs no udev of its own, but kernel device events
# reach this container, so udevd here applies the rules now and on every replug.

set -euo pipefail

sudo mkdir -p /etc/udev/rules.d
sudo cp "$(dirname "$0")/99-bt-hci-dongle.rules" /etc/udev/rules.d/
rules=/opt/SEGGER/JLink/99-jlink.rules
if [ -f "$rules" ]; then
    sudo cp "$rules" /etc/udev/rules.d/
else
    echo "start_udev.sh: $rules not found, skipping" >&2
fi
if ! pgrep -x systemd-udevd >/dev/null; then
    sudo /lib/systemd/systemd-udevd --daemon
fi
sudo udevadm trigger --subsystem-match=usb --action=add
sudo udevadm settle --timeout=10
