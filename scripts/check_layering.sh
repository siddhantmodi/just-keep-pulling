#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Check include rules from docs/architecture.md that the compiler can't:
# HAL interface headers (lib/hal/include/hal/*.hpp) must not include Zephyr or a
# Zephyr implementation, so logic that depends on them stays hardware-independent.

set -euo pipefail

cd "$(dirname "$0")/.."

mapfile -t interfaces < <(git ls-files --cached --others --exclude-standard -- 'lib/hal/include/hal/*.hpp' | grep -v '^lib/hal/include/hal/.*/')
if [ "${#interfaces[@]}" -eq 0 ]; then
    echo "No HAL interface headers to check."
    exit 0
fi

if grep -nE '#[[:space:]]*include[[:space:]]*<(zephyr/|hal/zephyr/)' "${interfaces[@]}"; then
    echo "error: HAL interface headers must not include Zephyr; put Zephyr code under lib/hal/include/hal/zephyr/." >&2
    exit 1
fi
echo "${#interfaces[@]} HAL interface headers are Zephyr-free."
