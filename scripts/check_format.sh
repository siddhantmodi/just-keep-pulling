#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Check the formatting of this repo's tracked C/C++ sources against .clang-format.
#
#   scripts/check_format.sh          # report violations, exit non-zero if any
#   scripts/check_format.sh --fix    # reformat in place
#
# Set CLANG_FORMAT to use a different clang-format binary (default: clang-format-20).

set -euo pipefail

cd "$(dirname "$0")/.."

CLANG_FORMAT="${CLANG_FORMAT:-clang-format-20}"

mapfile -t files < <(git ls-files --cached --others --exclude-standard -- '*.c' '*.cpp' '*.h' '*.hpp')
if [ "${#files[@]}" -eq 0 ]; then
    echo "No C/C++ sources to check."
    exit 0
fi

if [ "${1:-}" = "--fix" ]; then
    "$CLANG_FORMAT" -i "${files[@]}"
    echo "Formatted ${#files[@]} files."
else
    "$CLANG_FORMAT" --dry-run --Werror "${files[@]}"
    echo "${#files[@]} files are correctly formatted."
fi
