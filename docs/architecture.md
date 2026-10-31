# Firmware architecture

How application code is kept independent of hardware so it can be unit tested, how thin the C++ layer over Zephyr is, and why. These are working decisions, not settled ones: see [Revisiting these decisions](#revisiting-these-decisions).

## Goal

Application logic should be testable on `native_sim` without the board, quickly and deterministically, and without wrapping Zephyr just to be able to write C++.

## The deciding question

Unit tests run on `native_sim` with the **real Zephyr kernel**, so most of Zephyr is already available in tests. Before wrapping or mocking something, ask:

> Can the real thing run deterministically in a unit test?

If yes, use it directly. A seam (an injected, mockable dependency) is only needed where it can't.

| Dependency | Real thing in a unit test? | C++ wrapper | Test seam |
|---|---|---|---|
| Mutex, semaphore, message queue, threads | Yes | Thin RAII where it removes boilerplate (e.g. a lock guard) | None |
| zbus | Yes | Typed `ChannelRef<T>` | None: tests publish to and read real channels |
| Work queues, timers | Yes, on simulated time | Member-function callbacks, `std::chrono` | Only when logic decides *when* something happens (rule 4) |
| Settings (ZMS) | Yes, on the flash simulator | `fw::settings::load<T>` / `save` / `erase` | A domain interface for consumers (e.g. a calibration store), not a generic key-value mock |
| Peripherals (GPIO, ADC, sensors, PWM, I2C) | Partly (emulators), but awkward and per-board | `lib/hal` capability implementations | Capability interfaces in `lib/hal` |
| Large stacks (BLE, networking) | No | None | Split: thin adapter (integration-tested) + pure logic (unit-tested) |

## Rules

1. **Wrap only to remove a recurring C-ism**: `CONTAINER_OF` callback trampolines, manual lock/unlock, `k_timeout_t` instead of `std::chrono`, negative-errno ints instead of `fw::Result`. A wrapper that only renames a Zephyr function is not added; call Zephyr directly.
2. **Components depend on interfaces, so every layer can be unit tested against mocks of the layer below.** Hardware capabilities are interfaces in `lib/hal`; a service's interface is in its public header. Kernel objects, zbus and settings are not mocked: they run for real in tests (the table above).
3. **Logic doesn't own threads.** A component's logic is a synchronous object. Inputs arrive as method calls (a zbus message, a "timeout fired" event); outputs are publishes or HAL calls. The thread, work item or callback that drives it is plumbing. Tests call the methods directly: no sleeps, no thread synchronization.
4. **Seams are lean virtual interfaces.** An interface has pure virtual methods and a protected, non-virtual destructor (nothing is deleted through it); implementations are `final`. Objects are static and passed by reference: no `shared_ptr`, factories, heap or `std::function`. Hardware interfaces describe a capability (`hal::IDigitalOutput`, `hal::ITemperatureSensor`, a temperature in milli-°C), never *which* instance or *what it is for*: one `MockDigitalOutput` serves an LED, a buzzer and an enable line, and Zephyr types such as `sensor_channel` stay in the implementation. Concept-constrained templates are reserved for proven hot paths (e.g. per-sample code at interrupt rate).
5. **Large stacks are split, not wrapped.** BLE/networking code is a thin adapter (GATT table, callbacks, advertising) over pure logic (encoding, validation, state machines). The logic is unit tested; the adapter is covered by integration tests (Bumble).

## Integration tests

Integration tests run the **real app binary** on `native_sim`, not a cut-down fixture app, so they exercise exactly what ships. Bluetooth runs over a virtual radio: `pylib/zephyr_bumble` serves a Bumble controller to the app over TCP HCI (`--bt-dev`) and drives a Bumble central on the same virtual link. Twister launches the app (`harness: pytest`, its `dut` fixture), so the same tests could later drive a flashed board through a USB Bluetooth dongle.

- **What they check:** what a phone would see (discovery, reads, writes, ATT errors), plus the app's log for effects a central cannot observe (`unsupported opcode 0x67 ignored`).
- **Writes are acknowledged once accepted, then applied by the manager.** Tests wait for the resulting notification, state (`eventually`) or log line rather than assuming it is immediate.
- **`pylib/zephyr_bumble` stays app-agnostic.** UUIDs, names and expected values live in each app's `integration/` folder.

## Layers

```
lib/fw/            C++ over Zephyr, no hardware: fw::Result, fw::ChannelRef (typed zbus), fw::QueueRef (k_msgq), fw::ScopedLock, fw::settings (typed load/save)
lib/hal/           Hardware capabilities
  include/hal/                Interfaces only, no Zephyr includes (what logic depends on)
  include/hal/zephyr/         Thin Zephyr implementations, tested against native_sim emulators
  mocks/include/hal/mocks/    GoogleMocks of the interfaces (on the include path only with CONFIG_GTEST)
lib/gtest/         GoogleTest/GoogleMock as a Zephyr library
drivers/           Zephyr drivers owned by this repo: the ADS1220 load-cell ADC and its chip emulator, the emulated temperature sensor
include/jkp/       Their public headers (<jkp/drivers/...>)
dts/bindings/      Their devicetree bindings
app_<name>/src/    Components of one application
app_<name>/boards/ Per-board devicetree overlays: the only place an app differs between boards
```

In `lib/hal`, **the folder is the role and the file is named after the one type it defines**. The namespace follows the folder:

| Header | Type |
|---|---|
| `<hal/digital_output.hpp>` | `hal::IDigitalOutput` (interface) |
| `<hal/zephyr/gpio_output.hpp>` | `hal::zephyr::GpioOutput` |
| `<hal/mocks/mock_digital_output.hpp>` | `hal::mocks::MockDigitalOutput` |

- **Interfaces are named for the capability** (`I` prefix; the file drops it). Implementations are named for the Zephyr API behind them.
- **Input and output stay separate types**, even though both are built from a `gpio_dt_spec`. A pin that must be both (open-drain, 1-wire) gets its own type implementing both interfaces.
- **`scripts/check_layering.sh` enforces the Zephyr boundary.** It runs in CI and fails if an interface header includes Zephyr (it also checks the app layering below).
- **Pin direction is chosen where the app creates the object.** A `gpio_dt_spec` carries no direction, so `GpioOutput` configures whatever pin it is given as an output. Nothing in Zephyr or the HAL can catch a button pin passed to an output. Review the `GPIO_DT_SPEC_GET( ... )` line: the devicetree alias (`led0`, `sw0`) is where the intent is visible.

Within an app, components follow the layering proven in cho03, one folder per layer under `app_<name>/src/`:

- **`managers/`** own a thread and an event queue, and are the only callers of single-threaded services.
- **`services/`** wrap one subsystem or peripheral and have no thread of their own.
- **`modules/`** are pure logic with no Zephyr dependencies beyond `lib/fw`.
- **`channels/`** holds the app's zbus channels: the message contracts shared by every layer.

Dependencies point down: managers → services → modules. `scripts/check_layering.sh` fails CI if a service or module includes a manager's header, or a module includes a service's header or Zephyr. A component that more than one app needs moves out of its app to a repo-level folder shared by all apps once a second app actually needs it.

### Managers and zbus

A manager has its own event queue (`fw::QueueRef<Event>` over a `k_msgq`). A zbus **listener** on each channel the manager cares about copies the message into the queue and returns. The manager thread takes events off the queue and calls its logic's `handle(const Event &)`, which is what tests call. `app_main/src/managers/measurement_manager/` is the reference.

- **Every event is handled, in order**, with no allocation: the queue has fixed storage.
- **One queue for everything.** Timer expiries and interrupts post events to the same queue, without zbus channels of their own.
- **Publishers are never slowed or blocked.** The listener does a non-blocking put, which is safe from any thread or an ISR. A full queue drops the event, counts it and logs a warning. Size the queue deliberately (a Kconfig option per manager).
- **`Event` is a tagged union** (enum + union of payloads), not `std::variant`: `std::visit` costs too much code size. Slots are as big as the largest payload, so large data (buffers, certificates) travels separately, with a small event saying it is ready.

Which zbus mechanism to use:

| Need | Use |
|---|---|
| A manager reacting to messages | Listener → the manager's event queue (default) |
| The latest value, on demand (e.g. a BLE read of `led_state`) | `ChannelRef::read()`, no observer |
| A short, thread-safe reaction that touches no single-threaded service | A listener on its own |
| Plain or message subscriber | Not used by default. A message subscriber allocates a buffer on every publish (heap by default). Record why if one is introduced. |

## Component pattern

`app_main/src/services/load_cell_svc/` is the reference:

```
load_cell_svc/
  inc/load_cell_svc.hpp     Public: the ILoadCell interface and the accessor load_cell_svc::load_cell(). No concrete types, no Zephyr
  src/load_cell.hpp/.cpp    Logic: class LoadCell final : ILoadCell, taking hal::ILoadCellAdc & in its constructor; logging lives here
  src/load_cell_svc.cpp     Production instance: constinit Ads1220 on load-cell-adc + constinit LoadCell; load_cell() returns it
  mocks/mock_load_cell.hpp  MockLoadCell, for tests of components that use the load cell
  test/                     GoogleTest app: LoadCell with a MockLoadCellAdc (builds load_cell.cpp, not load_cell_svc.cpp)
```

1. **A component's public header holds its interface and an accessor** (`ILoadCell &load_cell()`).
2. **Logic classes take interfaces in their constructors, never accessors.** That is what makes them testable with mocks.
3. **Only the production file (`<component>.cpp`) uses accessors and binds hardware.** It is the one place where concrete types, devicetree aliases and other components meet. Zephyr's build-time macros (`K_THREAD_DEFINE`, `K_MUTEX_DEFINE`, `BT_GATT_SERVICE_DEFINE`, zbus listeners) also live in the production file that uses them, and are injected into logic classes by reference where needed (e.g. `ble_svc`'s client mutex).
4. **Production instances are `constinit`** when their dependencies are in the same file, so there is no constructor ordering between files. An instance that needs another component's accessor is built on first use instead (a function-local static, e.g. `measurement_manager`'s `measurement()`), before any thread that uses it starts.
5. **A component's mock ships with it** (`mocks/`), so every consumer's tests use the same one. HAL mocks live in `lib/hal/mocks`.
6. **Logic classes are named for the thing they model**, not the component: the namespace already says which component (`load_cell_svc::LoadCell`, not `load_cell_svc::LoadCellService`).

   **File names follow the component's stem.** A component is `<stem>_svc` or `<stem>_manager`. Its public header and production file take the component's name; its logic class and files, its mock and its test take the stem:

   | Component | Public header, production file | Logic | Mock | Test |
   |---|---|---|---|---|
   | `load_cell_svc` | `load_cell_svc.hpp`, `load_cell_svc.cpp` | `LoadCell` in `load_cell.hpp/.cpp` | `mock_load_cell.hpp` | `test_load_cell.cpp` |
   | `measurement_manager` | `measurement_manager.hpp`, `measurement_manager.cpp` | `Measurement` in `measurement.hpp/.cpp` | | `test_measurement.cpp` |

   Other files are named for what they hold (`ble_svc`'s `advertising.hpp`), never for a generic role (`manager.cpp`, `impl.cpp`, `utils.hpp`), so a file name alone says where it belongs.

7. **`docs/<app>.md` holds the app's class diagram** (Mermaid, rendered by GitHub): interfaces with their methods, implementations, modules, drivers and how they connect. A commit that adds a component or changes an interface updates it.

Tests construct a fresh instance per test with mocks: no singleton reset, no link-time seam. A component test builds only the logic `.cpp` files and sources the component's `Kconfig` (for its log level). A HAL implementation is tested once in `lib/hal/test` against an emulator; components never test it again.

**App code is identical on every board.** Board differences live in devicetree: production files bind an alias (`load-cell-adc`), and each board's overlay points it at real hardware or an emulator. Where Zephyr has no emulator for a peripheral (e.g. the die temperature sensor that `lib/hal`'s tests use), the repo adds a small emulated driver under `drivers/` with a test-control header, instead of `#if` branches in the app. Where the repo has its own driver, the emulator works at the chip's bus level instead (the ADS1220 emulator answers SPI commands on native_sim's SPI emulator bus and pulses an emulated /DRDY pin), so the real driver runs in tests too.

**Who depends on whom** (wired in production files):

| Component | Uses |
|---|---|
| `identity_svc` | settings (`identity/id`), the random number generator |
| `load_cell_svc` | `load-cell-adc` (ADS1220), settings (`load_cell/factor`, `load_cell/rate`) |
| `pmic_svc` | `pmic-charger` and `pmic-regulators` (the nPM1300) |
| `ble_svc` | `identity_svc::identity()`, channels `progressor_command` and `client_connection` (out) |
| `measurement_manager` | `load_cell_svc::load_cell()`, `ble_svc::client()`, `identity_svc::identity()`, channels `progressor_command` and `client_connection` (in), `battery` (read) |
| `power_manager` | `pmic_svc::pmic()`, `ble_svc::client()`, channel `battery` (out) |
| `debug_shell` | `identity_svc::identity()`, `load_cell_svc::load_cell()` (the thread-safe sample rate only), channel `battery` (read) |

## Build variants

An app's default build is its **release** build. `debug.conf` (added with `-DEXTRA_CONF_FILE=debug.conf`) makes the **debug** build: it enables Zephyr's shell with the app's debug commands (`app_main/src/debug_shell/`, e.g. overwriting the device ID) and asserts. Debug-only code depends on `CONFIG_SHELL`, so it cannot reach a release build; CI builds both and fails if a release build enables the shell. clang-tidy analyzes the debug build, which contains everything the release build does.

Shell commands run in the shell thread, so they only call services' thread-safe operations (documented on each), not the single-threaded ones a manager owns.

## Why C++ and not C

The testability comes from the rules above, not from the language. The same layering could be written in C with link seams or structs of function pointers. C++ makes it cheaper and catches more mistakes at compile time.

### What we gain

- **Seams without forwarding layers.** A virtual interface lets a test inject a GoogleMock directly, and production and mock implementations coexist in one binary. It is a function-pointer table that the compiler builds and type-checks. In C the options are:
  - link seams: one implementation per binary, a forwarding mock layer, global state to bind the mock;
  - hand-written function-pointer structs: the same runtime cost as a vtable, kept in sync by hand.
- **Errors that can't be ignored or confused.** `fw::Result<T>` (`std::expected`) is `[[nodiscard]]`, and it keeps the value and the error apart. In C the convention is `int` plus out-parameters: easy to drop, easy to mix up a count with an errno.
- **Types that carry meaning.**
  - `ChannelRef<T>` ties a zbus channel to its message type at compile time, where C zbus passes `void *` and a size.
  - `std::chrono` durations prevent ms/ticks/seconds mix-ups.
  - `enum class`, `std::span` and `std::array` carry their bounds and kind.
- **RAII for scoped resources.** A lock guard releases on every return path, including the early returns that `std::expected` code produces.
- **Compile-time checks.** `static_assert` on message layout, `constexpr` tables, `constinit` objects (including objects with vtables).
- **GoogleMock.** Mocks are generated from one `MOCK_METHOD` line per call. That style of mock injection needs the code under test to be C++. GoogleTest alone would work fine on C code.

### What it costs

- **Zephyr is a C project.** APIs, documentation, samples and most upstream reviewers are C-first, so there are fewer C++ examples to follow. Some static-definition macros use C-only constructs (compound literals, out-of-order designated initializers). These need workarounds or a small C file.
- **A language boundary.** Zephyr callbacks are C function pointers, so every callback goes through a trampoline. These are the C-isms `lib/fw` exists to absorb.
- **Footprint risk.**
  - `CONFIG_REQUIRES_FULL_LIBCPP` links the full C++ library, and careless template use duplicates code.
  - Some standard-library features allocate or throw and must not be used: `std::function`, `std::string`, `std::vector`, `shared_ptr`, iostreams. With exceptions off, a throwing path aborts, and the throwing paths drag in the exception runtime: in a past project (v5 gripper), heap ownership, `std::function` and that runtime cost ~23 KB, against ~2 KB for the vtables themselves.
  - Virtual calls cannot be inlined, and every method of a used class is linked in, called or not. Moving from concept templates to interfaces here cost +392 B flash and +24 B RAM on the nRF54L15 (68 B of vtables; the rest is calls that are no longer inlined).
  - Small costs creep in. For example, a non-const object holding a devicetree spec lives in RAM where a `const` C struct would sit in flash: `hal::zephyr::GpioOutput` costs 8 B of RAM.
- **A dialect to enforce.** C++23 with no exceptions, no RTTI, no heap in production paths, and `constinit` globals. clang-tidy enforces part of this; review covers the rest.
- **Toolchain coupling.** `std::expected` and other C++23 features depend on the Zephyr SDK's standard library version.
- **People.** Embedded engineers are, on average, more fluent in C. Interfaces at every layer are familiar to most C++ engineers; template-heavy code would be harder to review and debug.

## Revisiting these decisions

Revisit when one of these triggers fires, and record the outcome in the log below.

- **A wrapper is proposed** that doesn't remove a C-ism (rule 1).
- **A mock grows past roughly five methods.** The seam is probably a service's API rather than a capability.
- **A unit test needs `k_sleep` or thread synchronization to test logic.** The logic probably owns its timing or its thread (rule 3).
- **Flash or RAM jumps** in a way traceable to the C++ library, virtual dispatch or templates. Compare the `build` job's numbers between commits.
- **A Zephyr macro or subsystem needs a workaround** to be used from C++.
- **A new engineer finds the pattern harder than the problem it solves.**
- **Two components drive the same pin**, or a pin is bound with the wrong direction. Consider a debug-build "pin already claimed" check.
- **A manager event queue overflows**, or its slot size is driven by one large payload.
- **A hot path shows virtual-call overhead** (per-sample or ISR-rate code). Use a concept template there.
- **The app stays as small as today.** One LED doesn't need a manager thread; a listener alone would do. The thread pays off once several inputs share services.

Scheduled checkpoints:
- after the first real feature beyond the template app lands;
- at each product milestone.

### Decision log

| Date | Decision | Why |
|---|---|---|
| 2026-10-30 | Thin C++ over Zephyr; capability abstractions in `lib/hal` | Real kernel is available in `native_sim` tests; per-service platform abstractions duplicated mocks and implementations |
| 2026-10-30 | Concept-constrained templates over link seams (superseded below) | Tried in cho03 (`wifi_svc`): mocks inject directly, both implementations coexist in one binary, no forwarding layer |
| 2026-10-30 | Managers: zbus listener → own `k_msgq` event queue, not zbus subscribers | Every event handled with no allocation; one queue also serves timers and ISRs; publishers never block. Message subscribers allocate per publish |
| 2026-10-30 | Manager events are a tagged union, not `std::variant` | `std::visit` has blown up code size in past projects |
| 2026-10-30 | Lean virtual interfaces at every layer replace concept templates | Templates spread through every layer (logic in headers, `Manager<Led<GpioOutput>>`) and needed adapters where free-function APIs met injected objects. Measured on the nRF54L15: +392 B flash, +24 B RAM |
| 2026-10-30 | Static objects passed by reference; no `shared_ptr`, factories or `std::function` | In the v5 gripper those cost ~23 KB (with the exception runtime they pull in) against ~2 KB of vtables; there they only hid concrete types, which production files do here |
| 2026-10-30 | Each component's production file binds its hardware and exposes an interface accessor | Wiring everything in `main()` made `main` the manager thread, which does not scale to several threads, and fights Zephyr's build-time macros |
