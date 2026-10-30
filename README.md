# just_keep_pulling

Zephyr RTOS based firmware for a dynamometer for measuring finger strength for rock climbers. Based on the open-source Crimpdeq.

Built on the nRF Connect SDK (NCS) for the nRF54L15 DK, in C++23, with GoogleTest unit tests that run on `native_sim`.

## Repository layout

```
west.yml              West manifest: sdk-nrf (allowlisted) and googletest, fetched into ../deps
west_setup.sh         Initialize/update the west workspace (run by the devcontainer and CI)
zephyr/module.yml     This repo is a Zephyr module, so lib/ is available to every app and test
lib/
  fw/                 Header-only C++ helpers (<fw/error.hpp>: fw::Result error model)
  hal/                Hardware capability concepts (<hal/...>), Zephyr implementations (<hal/zephyr/...>), GoogleMocks (<hal/mocks/...>)
  gtest/              GoogleTest/GoogleMock as a Zephyr library (CONFIG_GTEST), test-only
docs/architecture.md  Design rules, layers, why C++, and when to revisit them
app_main/             The main firmware application
  src/main.cpp
  src/<component>/    One folder per component:
    inc/              Public API
    src/              Implementation
    test/             GoogleTest unit test (its own small native_sim app)
scripts/              check_format.sh, run_clang_tidy.py
.github/workflows/    CI
```

Other applications (factory, calibration, samples) go in their own top-level `app_<name>/` directories.

The west workspace topdir is the **parent** of this repo; dependencies live in `../deps/`. In the devcontainer that is `/home/user/workspace`, with this repo at `/home/user/workspace/just-keep-pulling`.

## Patterns

- **Errors:** anything that can fail returns `fw::Result<T>` (`std::expected<T, std::errc>`). Zephyr return codes are converted with `fw::err::check()` / `fw::err::from_zephyr()`.
- **C++:** C++23, no exceptions, no RTTI.
- **Hardware seams:** a component's logic is a class template over the hardware capabilities it needs, constrained by `lib/hal` concepts (e.g. `hal::DigitalOutput`). Production code instantiates it with the Zephyr implementation (`hal::zephyr::GpioOutput` on a devicetree pin); unit tests instantiate it with a GoogleMock (`hal::mocks::MockDigitalOutput`). The public API is a set of free functions over one production instance (see `app_main/src/led_svc/`).
- **Kernel, zbus, settings:** used for real in unit tests (the tests run on `native_sim`), wrapped only where it removes C boilerplate. See [docs/architecture.md](docs/architecture.md) for the rules.
- **Unit tests:** GoogleTest, built for `native_sim` and run by Twister (`harness: gtest`). A test's `prj.conf` sets `CONFIG_GTEST=y`; tests are tagged `unit`.

## Building

From the workspace topdir (`/home/user/workspace` in the devcontainer):

```sh
west build -b nrf54l15dk/nrf54l15/cpuapp -d build/app_main just-keep-pulling/app_main
west flash -d build/app_main
```

`app_main` also builds and runs on `native_sim/native/64`.

## Testing and checks

Run these from this repo's directory; CI runs the same commands.

```sh
# Unit tests (add --coverage --coverage-tool gcovr --enable-asan --enable-ubsan as CI does)
west twister -T . -p native_sim/native/64 --tag unit --inline-logs

# Formatting (--fix to reformat in place)
scripts/check_format.sh

# Include rules (HAL concept headers must not include Zephyr)
scripts/check_layering.sh

# clang-tidy: builds app_main for native_sim with clang, then analyzes production sources
scripts/run_clang_tidy.py
```

## CI

`.github/workflows/ci.yml` runs in the devcontainer image (`ghcr.io/siddhantmodi/zephyr-dev-nordic`):

| Job | What it does |
|---|---|
| `format` | `scripts/check_format.sh` and `scripts/check_layering.sh` |
| `unit` | Twister unit tests on `native_sim/native/64` with coverage, ASan and UBSan; uploads results and the coverage report |
| `tidy` | `scripts/run_clang_tidy.py` for each app |
| `build` | Builds each app for its boards once the checks pass; uploads `.hex`/`.bin`/`.elf`, `.config` and checksums |
