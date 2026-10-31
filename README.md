# just_keep_pulling

Zephyr RTOS based firmware for a dynamometer for measuring finger strength for rock climbers. Based on the open-source Crimpdeq.

Built on the nRF Connect SDK (NCS) for the nRF54L15 DK, in C++23, with GoogleTest unit tests and Bumble BLE integration tests that run on `native_sim`.

The template app, `app_main`, exposes a custom BLE GATT service (advertised as "Just Keep Pulling"): an LED characteristic (read/write, `uint8` 0/1) and the die temperature (read, `int16` little-endian, hundredths of a degree Celsius). The service is open: no pairing is required. See `app_main/src/services/ble_svc/inc/ble_svc.hpp` for the UUIDs.

## Repository layout

```
west.yml              West manifest: sdk-nrf (allowlisted) and googletest, fetched into ../deps
west_setup.sh         Initialize/update the west workspace (run by the devcontainer and CI)
zephyr/module.yml     This repo is a Zephyr module, so lib/, drivers/ and dts/ are available to every app and test
lib/
  fw/                 Header-only C++ helpers: fw::Result, fw::ChannelRef (zbus), fw::QueueRef (k_msgq), fw::ScopedLock
  hal/                Hardware capability interfaces (<hal/...>), Zephyr implementations (<hal/zephyr/...>), GoogleMocks (<hal/mocks/...>)
  gtest/              GoogleTest/GoogleMock as a Zephyr library (CONFIG_GTEST), test-only
drivers/              Repo-owned Zephyr drivers (emulated temperature sensor for native_sim)
include/jkp/          Their headers; dts/bindings/ holds their devicetree bindings
docs/architecture.md  Design rules, layers, why C++, and when to revisit them
app_main/             The main firmware application
  boards/             Devicetree overlays per board (aliases led0, die-temp)
  src/main.cpp
  src/channels/       zbus channels: the message contracts shared by every layer
  src/managers/       Own a thread and an event queue (app_manager)
  src/services/       One subsystem or peripheral each (led_svc, temp_svc, ble_svc)
  src/modules/        Pure logic, no Zephyr (none yet)
    <component>/      Each component:
      inc/            Public API
      src/            Implementation
      mocks/          GoogleMock of its interface, for consumers' tests
      test/           GoogleTest unit test (its own small native_sim app)
  integration/        pytest + Bumble tests of the whole app over BLE (app_main/testcase.yaml)
pylib/zephyr_bumble/  Reusable pytest plugin: Bumble virtual central for native_sim BLE apps
requirements-test.txt Python dependencies of the integration tests (installed by west_setup.sh)
scripts/              check_format.sh, check_layering.sh, run_clang_tidy.py
.github/workflows/    CI
```

Other applications (factory, calibration, samples) go in their own top-level `app_<name>/` directories.

The west workspace topdir is the **parent** of this repo; dependencies live in `../deps/`. In the devcontainer that is `/home/user/workspace`, with this repo at `/home/user/workspace/just-keep-pulling`.

## Patterns

- **Errors:** anything that can fail returns `fw::Result<T>` (`std::expected<T, std::errc>`). Zephyr return codes are converted with `fw::err::check()` / `fw::err::from_zephyr()`.
- **C++:** C++23, no exceptions, no RTTI.
- **Interfaces at every layer:** a component's public header holds an interface (`led_svc::ILed`) and an accessor to its production instance (`led_svc::led()`). Logic classes take interfaces in their constructors (hardware ones from `lib/hal`, e.g. `hal::IDigitalOutput`), so unit tests inject GoogleMocks; each component's production file binds the real hardware (`hal::zephyr::GpioOutput` on `led0`). See `app_main/src/services/led_svc/`.
- **Kernel, zbus, settings:** used for real in unit tests (the tests run on `native_sim`), wrapped only where it removes C boilerplate. See [docs/architecture.md](docs/architecture.md) for the rules.
- **Managers and zbus:** a manager thread is the only caller of single-threaded services. zbus listeners copy messages into its event queue (see `app_main/src/managers/app_manager/` and [docs/architecture.md](docs/architecture.md)).
- **Unit tests:** GoogleTest, built for `native_sim` and run by Twister (`harness: gtest`). A test's `prj.conf` sets `CONFIG_GTEST=y`; tests are tagged `unit`.
- **Integration tests:** the real app on `native_sim`, driven over BLE by a Bumble virtual central (`harness: pytest`, `pylib/zephyr_bumble`); tagged `integration`.

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

# Integration tests: app_main on native_sim against a Bumble virtual BLE central
west twister -T . -p native_sim/native/64 --tag integration --inline-logs

# Formatting (--fix to reformat in place)
scripts/check_format.sh

# Include rules (HAL interface headers are Zephyr-free; managers -> services -> modules)
scripts/check_layering.sh

# clang-tidy: builds app_main for native_sim with clang, then analyzes production sources
scripts/run_clang_tidy.py
```

## CI

`.github/workflows/ci.yml` runs in the devcontainer image (`ghcr.io/siddhantmodi/zephyr-dev-nordic`). Two caches speed it up: the west dependencies (`../deps`, keyed on `west.yml`, which pins `sdk-nrf` to a tag) and a ccache per job (Zephyr builds use it automatically; each job prints its hit rate at the end).

| Job | What it does |
|---|---|
| `format` | `scripts/check_format.sh` and `scripts/check_layering.sh` |
| `unit` | Twister unit tests on `native_sim/native/64` with coverage, ASan and UBSan; uploads results and the coverage report |
| `integration` | Twister integration tests (Bumble over BLE) on `native_sim/native/64` after `unit`; uploads results and app logs |
| `tidy` | `scripts/run_clang_tidy.py` for each app |
| `build` | Builds each app for its boards once the checks pass; uploads `.hex`/`.bin`/`.elf`, `.config` and checksums |
