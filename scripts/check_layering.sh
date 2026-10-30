#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Check include rules from docs/architecture.md that the compiler can't:
# HAL concept headers (lib/hal/include/hal/*.hpp) must not include Zephyr or a
# Zephyr implementation, so logic that depends on them stays hardware-independent.

set -euo pipefail

cd "$(dirname "$0")/.."

mapfile -t concepts < <(git ls-files --cached --others --exclude-standard -- 'lib/hal/include/hal/*.hpp' | grep -v '^lib/hal/include/hal/.*/')
if [ "${#concepts[@]}" -eq 0 ]; then
    echo "No HAL concept headers to check."
    exit 0
fi

if grep -nE '#[[:space:]]*include[[:space:]]*<(zephyr/|hal/zephyr/)' "${concepts[@]}"; then
    echo "error: HAL concept headers must not include Zephyr; put Zephyr code under lib/hal/include/hal/zephyr/." >&2
    exit 1
fi
echo "${#concepts[@]} HAL concept headers are Zephyr-free."
