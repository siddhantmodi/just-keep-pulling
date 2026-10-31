#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Check include rules from docs/architecture.md that the compiler can't:
# - HAL interface headers (lib/hal/include/hal/*.hpp) must not include Zephyr or
#   a Zephyr implementation, so logic that depends on them stays hardware-independent.
# - Within an app (app_*/src/), dependencies point down the layers:
#   managers -> services -> modules. Services and modules must not include a
#   manager's headers; modules must not include a service's headers or Zephyr.

set -euo pipefail

cd "$(dirname "$0")/.."

failed=0

files() {
    # Layer folders may not exist yet (e.g. modules/); git warns about those.
    git ls-files --cached --others --exclude-standard -- "$@" 2>/dev/null
}

# Fail if any of the files in "$2..." includes one of the headers listed in $1 (one basename per line).
check_no_includes() {
    local headers="$1" message="$2"
    shift 2
    [ -n "$headers" ] && [ "$#" -gt 0 ] || return 0
    local pattern
    pattern="#[[:space:]]*include[[:space:]]*\"($(paste -sd'|' <<<"$headers" | sed 's/\./\\./g'))\""
    if grep -nE "$pattern" "$@"; then
        echo "error: $message" >&2
        failed=1
    fi
}

mapfile -t interfaces < <(files 'lib/hal/include/hal/*.hpp' | grep -v '^lib/hal/include/hal/.*/' || true)
if [ "${#interfaces[@]}" -gt 0 ] && grep -nE '#[[:space:]]*include[[:space:]]*<(zephyr/|hal/zephyr/)' "${interfaces[@]}"; then
    echo "error: HAL interface headers must not include Zephyr; put Zephyr code under lib/hal/include/hal/zephyr/." >&2
    failed=1
fi
echo "${#interfaces[@]} HAL interface headers checked."

for src in app_*/src; do
    [ -d "$src" ] || continue
    manager_headers=$(files "$src/managers/*/inc/*.hpp" "$src/managers/*/src/*.hpp" | xargs -r -n1 basename)
    service_headers=$(files "$src/services/*/inc/*.hpp" "$src/services/*/src/*.hpp" | xargs -r -n1 basename)
    mapfile -t services < <(files "$src/services/*.cpp" "$src/services/*.hpp")
    mapfile -t modules < <(files "$src/modules/*.cpp" "$src/modules/*.hpp")

    check_no_includes "$manager_headers" "$src: services and modules must not depend on managers." "${services[@]}" "${modules[@]}"
    check_no_includes "$service_headers" "$src: modules must not depend on services." "${modules[@]}"
    if [ "${#modules[@]}" -gt 0 ] && grep -nE '#[[:space:]]*include[[:space:]]*<zephyr/' "${modules[@]}"; then
        echo "error: $src: modules are pure logic and must not include Zephyr." >&2
        failed=1
    fi
    echo "$src: ${#services[@]} service and ${#modules[@]} module files checked."
done

exit "$failed"
