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
| Settings / NVS / ZMS | Yes, on the flash simulator | Typed load/save helper (when first needed) | A domain concept for consumers (e.g. a calibration store), not a generic key-value mock |
| Peripherals (GPIO, ADC, sensors, PWM, I2C) | Partly (emulators), but awkward and per-board | `lib/hal` capability implementations | Capability concepts in `lib/hal` |
| Large stacks (BLE, networking) | No | None | Split: thin adapter (integration-tested) + pure logic (unit-tested) |

## Rules

1. **Wrap only to remove a recurring C-ism**: `CONTAINER_OF` callback trampolines, manual lock/unlock, `k_timeout_t` instead of `std::chrono`, negative-errno ints instead of `fw::Result`. A wrapper that only renames a Zephyr function is not added; call Zephyr directly.
2. **Seam only what can't run deterministically in a test**: hardware, and time when logic schedules things.
3. **Logic doesn't own threads.** A component's logic is a synchronous object. Inputs arrive as method calls (a zbus message, a "timeout fired" event); outputs are publishes or HAL calls. The thread, work item or callback that drives it is plumbing. Tests call the methods directly: no sleeps, no thread synchronization.
4. **Seams are capability concepts with real Zephyr implementations**, e.g. `hal::DigitalOutput`, `hal::SensorChannel`, a one-shot timer. A concept describes *what the hardware can do*, never *which* instance or *what it is for*: one `MockDigitalOutput` serves an LED, a buzzer and an enable line. The app picks the instance (the devicetree node) where it creates the production object.
5. **Large stacks are split, not wrapped.** BLE/networking code is a thin adapter (GATT table, callbacks, advertising) over pure logic (encoding, validation, state machines). The logic is unit tested; the adapter is covered by integration tests (Bumble).

## Layers

```
lib/fw/            C++ over Zephyr, no hardware: fw::Result error model; zbus, kernel-object and settings helpers as they become needed
lib/hal/           Hardware capabilities
  include/hal/                Concepts only, no Zephyr includes (what logic depends on)
  include/hal/zephyr/         Thin Zephyr implementations, tested against native_sim emulators
  mocks/include/hal/mocks/    GoogleMocks of the concepts (on the include path only with CONFIG_GTEST)
lib/gtest/         GoogleTest/GoogleMock as a Zephyr library
app_<name>/src/    Components of one application
```

In `lib/hal`, **the folder is the role and the file is named after the one type it defines**. The namespace follows the folder:

| Header | Type |
|---|---|
| `<hal/digital_output.hpp>` | `hal::DigitalOutput` (concept) |
| `<hal/zephyr/gpio_output.hpp>` | `hal::zephyr::GpioOutput` |
| `<hal/mocks/mock_digital_output.hpp>` | `hal::mocks::MockDigitalOutput` |

- **Concepts are named for the capability.** Implementations are named for the Zephyr API behind them.
- **Input and output stay separate types**, even though both are built from a `gpio_dt_spec`. A pin that must be both (open-drain, 1-wire) gets its own type satisfying both concepts, not a shared base.
- **`scripts/check_layering.sh` enforces the Zephyr boundary.** It runs in CI and fails if a concept header includes Zephyr.
- **Pin direction is chosen where the app creates the object.** A `gpio_dt_spec` carries no direction, so `GpioOutput` configures whatever pin it is given as an output. Nothing in Zephyr or the HAL can catch a button pin passed to an output. Review the `GPIO_DT_SPEC_GET( ... )` line: the devicetree alias (`led0`, `sw0`) is where the intent is visible.

Within an app, components follow the layering proven in cho03:

- **Managers** own a thread and a zbus loop, and coordinate services.
- **Services** wrap one subsystem or peripheral and have no thread of their own.
- **Modules** are pure logic with no Zephyr dependencies beyond `lib/fw`.

## Component pattern

`app_main/src/led_svc/` is the reference:

```
led_svc/
  inc/led_svc.hpp        Public API: free functions returning fw::Result
  src/led.hpp            Logic: template<hal::DigitalOutput Output> class Led
  src/led_svc.cpp        Production binding: constinit hal::zephyr::GpioOutput on led0 + Led, the free functions, logging
  test/                  GoogleTest app: Led<NiceMock<MockDigitalOutput>>
```

- `<component>.hpp`/`.cpp` (here `led_svc`) is always the public API and its production binding. The logic class is named for the thing it models, not the component: the namespace already says which component it belongs to (`led_svc::Led`, not `led_svc::LedService`). Its header is named after it (`led.hpp`).
- Logic is a class template over the capabilities it needs, constrained by their concepts. A misuse fails at compile time with the concept's name.
- The production instance is `constinit`, so it is built at compile time with no global-constructor ordering.
- Tests construct a fresh instance per test with mocks. They need no singleton reset and no link-time seam.
- A HAL implementation is tested once in `lib/hal/test` against the emulator. Components never test it again.

## Why C++ and not C

The testability comes from the rules above, not from the language. The same layering could be written in C with link seams or structs of function pointers. C++ makes it cheaper and catches more mistakes at compile time.

### What we gain

- **Zero-cost, compile-time seams.** Concept-constrained templates let a test inject a GoogleMock directly; production code calls the Zephyr implementation with static dispatch, which the compiler can inline. In C the options are:
  - link seams: one implementation per binary, a forwarding mock layer, global state to bind the mock;
  - function-pointer tables: indirect calls, no inlining, a struct to keep in sync by hand.
- **Errors that can't be ignored or confused.** `fw::Result<T>` (`std::expected`) is `[[nodiscard]]`, and it keeps the value and the error apart. In C the convention is `int` plus out-parameters: easy to drop, easy to mix up a count with an errno.
- **Types that carry meaning.**
  - `ChannelRef<T>` ties a zbus channel to its message type at compile time, where C zbus passes `void *` and a size.
  - `std::chrono` durations prevent ms/ticks/seconds mix-ups.
  - `enum class`, `std::span` and `std::array` carry their bounds and kind.
- **RAII for scoped resources.** A lock guard releases on every return path, including the early returns that `std::expected` code produces.
- **Compile-time checks.** `static_assert` on concepts and message layout, `constexpr` tables, `constinit` objects.
- **GoogleMock.** Mocks are generated from one `MOCK_METHOD` line per call. That style of mock injection needs the code under test to be C++. GoogleTest alone would work fine on C code.

### What it costs

- **Zephyr is a C project.** APIs, documentation, samples and most upstream reviewers are C-first, so there are fewer C++ examples to follow. Some static-definition macros use C-only constructs (compound literals, out-of-order designated initializers). These need workarounds or a small C file.
- **A language boundary.** Zephyr callbacks are C function pointers, so every callback goes through a trampoline. These are the C-isms `lib/fw` exists to absorb.
- **Footprint risk.**
  - `CONFIG_REQUIRES_FULL_LIBCPP` links the full C++ library, and careless template use duplicates code.
  - Some standard-library features allocate or throw and must not be used: `std::function`, `std::string`, `std::vector`, iostreams. With exceptions off, a throwing path aborts.
  - Small costs creep in. For example, a non-const object holding a devicetree spec lives in RAM where a `const` C struct would sit in flash: `hal::zephyr::GpioOutput` costs 8 B of RAM.
- **Templates put logic in headers.** That means longer compiles and longer error messages; concepts keep the messages readable.
- **A dialect to enforce.** C++23 with no exceptions, no RTTI, no heap in production paths, and `constinit` globals. clang-tidy enforces part of this; review covers the rest.
- **Toolchain coupling.** `std::expected` and other C++23 features depend on the Zephyr SDK's standard library version.
- **People.** Embedded engineers are, on average, more fluent in C. Template-heavy code is harder to review and debug, with mangled names and deep instantiation stacks.

## Revisiting these decisions

Revisit when one of these triggers fires, and record the outcome in the log below.

- **A wrapper is proposed** that doesn't remove a C-ism (rule 1).
- **A mock grows past roughly five methods.** The seam is probably a service's API rather than a capability.
- **A unit test needs `k_sleep` or thread synchronization to test logic.** The logic probably owns its timing or its thread (rule 3).
- **Flash or RAM jumps** in a way traceable to templates or the C++ library. Compare the `build` job's numbers between commits.
- **A Zephyr macro or subsystem needs a workaround** to be used from C++.
- **A new engineer finds the pattern harder than the problem it solves.**
- **Two components drive the same pin**, or a pin is bound with the wrong direction. Consider a debug-build "pin already claimed" check.

Scheduled checkpoints:
- after the first real (non-template) feature lands;
- at each product milestone.

### Decision log

| Date | Decision | Why |
|---|---|---|
| 2026-10-30 | Thin C++ over Zephyr; seams only for hardware and scheduling; capability concepts in `lib/hal` | Real kernel is available in `native_sim` tests; per-service platform concepts duplicated mocks and implementations |
| 2026-10-30 | Concept-constrained templates over link seams | Tried in cho03 (`wifi_svc`): mocks inject directly, both implementations coexist in one binary, no forwarding layer |
