#!/bin/bash

set -e  # Exit on any error

PROJECT_DIR=$(cd "$(dirname "$0")" && pwd)  # Get the script directory (absolute)
TOPDIR=$(dirname "$PROJECT_DIR")            # West workspace is the parent of this repo
WEST_DIR="${TOPDIR}/.west"

cd "$TOPDIR"

if [ ! -d "$WEST_DIR" ]; then
    echo "No .west directory found. Initializing West..."
    west init -l "$PROJECT_DIR"
else
    echo ".west directory already exists. Skipping initialization."
fi

west update --narrow -o=--depth=1
west zephyr-export
west packages pip --install --ignore-venv-check

# sdk-nrf and mcuboot don't register their requirements with `west packages`
python3 -m pip install \
    -r "${TOPDIR}/deps/nrf/scripts/requirements.txt" \
    -r "${TOPDIR}/deps/bootloader/mcuboot/scripts/requirements.txt"

# Integration tests: Bumble and this repo's pytest plugin (pylib/zephyr_bumble)
python3 -m pip install \
    -r "${PROJECT_DIR}/requirements-test.txt" \
    -e "${PROJECT_DIR}/pylib/zephyr_bumble"
