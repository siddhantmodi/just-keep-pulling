#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run clang-tidy over an app's production sources.

Builds the app for native_sim with the host clang toolchain, so the compile
database already has clang-native flags, then runs clang-tidy (configured by
the repo-root .clang-tidy) on every production translation unit: files under
<app>/src/ and lib/, excluding tests, integration fixtures, fakes and the
GoogleTest wrapper. Exits non-zero on any finding.

  scripts/run_clang_tidy.py                        # build + tidy app_main
  scripts/run_clang_tidy.py --app app_factory      # a different app
  scripts/run_clang_tidy.py --no-build             # reuse the existing build
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_APP = "app_main"
DEFAULT_BOARD = "native_sim/native/64"
DEFAULT_CLANG_TIDY = "clang-tidy-20"
EXCLUDE_MARKERS = ("/test/", "/integration/", "/fakes/", "/lib/gtest/")


def find_llvm_path() -> str:
    """Locate the LLVM install dir (the one containing bin/clang)."""
    if env := os.environ.get("LLVM_TOOLCHAIN_PATH"):
        return env
    for name in ("clang-20", "clang"):
        if path := shutil.which(name):
            prefix = Path(path).resolve().parent.parent
            if (prefix / "bin" / "clang").exists():
                return str(prefix)
    sys.exit("error: could not locate LLVM. Set LLVM_TOOLCHAIN_PATH to the directory containing bin/clang.")


def build_with_clang(app_dir: Path, board: str, build_dir: Path) -> None:
    llvm = find_llvm_path()
    print(f"Building {app_dir.name} for {board} with clang ({llvm}) -> {build_dir}", flush=True)
    cmd = [
        "west", "build", "--no-sysbuild", "-p", "always", "-b", board, "-d", str(build_dir), str(app_dir),
        "--", "-DTOOLCHAIN_VARIANT_COMPILER=llvm", f"-DLLVM_TOOLCHAIN_PATH={llvm}",
    ]
    # A debug build includes everything a release build does, plus debug-only code (e.g. shell commands).
    if (app_dir / "debug.conf").exists():
        cmd.append("-DEXTRA_CONF_FILE=debug.conf")
    if subprocess.run(cmd, cwd=REPO_ROOT).returncode != 0:
        sys.exit("error: native_sim clang build failed.")


def is_production(path: str, app_dir: Path) -> bool:
    roots = (f"{app_dir}/src/", f"{REPO_ROOT}/lib/")
    return path.startswith(roots) and not any(marker in path for marker in EXCLUDE_MARKERS)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", default=DEFAULT_APP, help=f"app directory, relative to the repo (default: {DEFAULT_APP})")
    parser.add_argument("--board", default=DEFAULT_BOARD, help=f"native_sim board (default: {DEFAULT_BOARD})")
    parser.add_argument("--build-dir", type=Path, help="build directory (default: build/tidy-<app>)")
    parser.add_argument("--no-build", action="store_true", help="reuse an existing build directory")
    parser.add_argument("--clang-tidy", default=DEFAULT_CLANG_TIDY, help=f"clang-tidy binary (default: {DEFAULT_CLANG_TIDY})")
    args = parser.parse_args()

    app_dir = (REPO_ROOT / args.app).resolve()
    build_dir = (args.build_dir or REPO_ROOT / "build" / f"tidy-{app_dir.name}").resolve()

    if not args.no_build:
        build_with_clang(app_dir, args.board, build_dir)

    database = build_dir / "compile_commands.json"
    if not database.is_file():
        sys.exit(f"error: {database} not found. Build first (drop --no-build).")

    entries = json.loads(database.read_text())
    files = sorted({entry["file"] for entry in entries if is_production(entry["file"], app_dir)})
    if not files:
        sys.exit(f"error: no production sources for {args.app} in {database}.")

    print(f"Running clang-tidy on {len(files)} files...", flush=True)
    return subprocess.run([args.clang_tidy, "--quiet", "-p", str(build_dir), *files], cwd=REPO_ROOT).returncode


if __name__ == "__main__":
    sys.exit(main())
