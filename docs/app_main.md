# app_main design

How `app_main`'s interfaces, classes, modules and drivers fit together, and what each offers. Keep this diagram in step with the code: a change to an interface or a new component updates it in the same commit.

Conventions: `<<interface>>` classes are the seams that tests mock; a solid arrow is a reference held by an object, a dashed arrow is a call or use. Methods return `fw::Result` (`Result~T~` here) unless shown otherwise. Mocks (`hal::mocks::MockLoadCellAdc`, `MockBatteryCharger`, `MockPowerOff`, `MockMotionSensor`, `load_cell_svc::MockLoadCell`, `ble_svc::MockClient`, `pmic_svc::MockPmic`, `motion_svc::MockMotion`, `led_svc::MockLed`) implement each interface for unit tests and are left out to keep the diagram readable.

```mermaid
classDiagram
    direction TB

    class main {
        <<entry point>>
        main()
    }

    namespace measurement_manager {
        class Measurement {
            <<manager thread>>
            +start() Result
            +handle(Event) Result
            +measuring() bool
            +sample() Result
            +dropped_batches() uint32
        }
        class Event {
            <<tagged union>>
            command: progressor::Command
            client_connection: ClientConnection
            config_command: ConfigCommand
        }
    }

    namespace power_manager {
        class Power {
            <<manager thread: battery>>
            +check() Result
            +next_check() milliseconds
            is_low(BatteryStatus) bool
        }
        class Indicator {
            <<manager thread: status LED>>
            +start() Result
            +on_client_connection(bool) Result
            +on_battery(mV) Result
            +on_sleep(bool) Result
            +on_blink_step() Result
            +blinking() bool
        }
        class Activity {
            <<manager thread: inactivity>>
            +on_command(Opcode)
            +on_configuration()
            +on_client_connection(bool)
            +on_measurement(bool)
            +check() Result
            +idle() seconds
        }
    }

    class debug_shell {
        <<debug builds only>>
        jkp id [set hex]
        jkp rate [set SPS]
        jkp battery
    }

    namespace identity_svc {
        class IIdentity {
            <<interface>>
            +init() Result
            +id() DeviceId
            +set_id(DeviceId) Result
        }
        class Identity {
            -DeviceId id_
        }
        class DeviceId {
            <<6 bytes, MSB first>>
            parse_device_id(hex) Result~DeviceId~
            format_device_id(DeviceId) text
        }
    }

    namespace load_cell_svc {
        class ILoadCell {
            <<interface>>
            +init() Result
            +sample_rate() SampleRate
            +set_sample_rate(SampleRate) Result
            +start(SampleRate) Result
            +read(timeout) Result~ForceSample~
            +stop() Result
            +lost_samples() uint32
            +tare() Result
            +add_calibration_point(kg) Result~size_t~
            +reset_calibration() Result
            +calibration_factor() float
        }
        class LoadCell {
            -int32 tare_
            -float factor_
            -optional~SampleRate~ running_rate_
            -atomic~SampleRate~ configured_rate_
            -Point points_[20]
            -average_raw(SampleRate, duration) Result~double~
        }
        class ForceSample {
            +float kg
            +uint64 timestamp_us
        }
    }

    namespace ble_svc {
        class IClient {
            <<interface>>
            +send(bytes) Result
            +att_mtu() uint16
            +set_streaming(bool) Result
            +disconnect() Result
        }
        class Client {
            <<GATT adapter>>
            Progressor service 7e4e1701
            data point 7e4e1702: notify
            control point 7e4e1703: write
            advertised_name(DeviceId) Progressor_NNNN
            Device Information and Battery services
            battery_level_percent(mV) uint8
        }
    }

    namespace jkp_svc {
        class JkpService {
            <<GATT service, static>>
            JustKeepPulling service a6bd0001
            calibration a6bd0002: read, notify
            add calibration point a6bd0003: write kg
            reset calibration a6bd0004: write 0x01
            sample rate a6bd0005: read, write
            device ID a6bd0006: read
        }
        class jkp_protocol {
            <<pure>>
            encode_calibration(status, points, factor) bytes
            decode_calibration_kg(bytes) Result~float~
            decode_reset_calibration(bytes) Result
            encode_sample_rate(SampleRate) bytes
            decode_sample_rate(bytes) Result~SampleRate~
        }
    }

    namespace pmic_svc {
        class IPmic {
            <<interface>>
            +init() Result
            +read_battery() Result~BatteryStatus~
            +ship_mode() Result
        }
        class Pmic
    }

    namespace motion_svc {
        class IMotion {
            <<interface>>
            +init() Result
            +moved() bool
            +sleep_until_moved() Result
        }
        class Motion
    }

    namespace led_svc {
        class ILed {
            <<interface>>
            +init() Result
            +show(Color) Result
            +color() Color
        }
        class Led {
            three IDigitalOutput, one color at a time
        }
    }

    namespace channels {
        class progressor_command {
            <<zbus>>
            progressor::Command
        }
        class client_connection {
            <<zbus>>
            bool connected
        }
        class battery {
            <<zbus>>
            hal::BatteryStatus
        }
        class measurement_state {
            <<zbus>>
            bool measuring
        }
        class config_command {
            <<zbus>>
            add_calibration_point, reset_calibration, set_sample_rate
        }
        class calibration_state {
            <<zbus>>
            status, points, factor
        }
    }

    namespace modules {
        class calibration {
            <<module, pure>>
            default_factor = 0.109 g per code
            to_kg(raw, tare, factor) float
            fit_factor(Point[]) Result~float~
            is_plausible(factor) bool
        }
        class progressor {
            <<module, pure>>
            decode_command(bytes) Result~Command~
            encode_weight_measurements(Sample[]) Result~Packet~
            encode_battery_voltage(mV) Packet
            encode_app_version(text) Packet
            encode_progressor_id(id) Packet
            encode_low_power_warning() Packet
            samples_per_packet(att_mtu) size_t
        }
    }

    namespace hal {
        class ILoadCellAdc {
            <<interface>>
            +init() Result
            +start(SampleRate) Result
            +read(timeout) Result~LoadCellSample~
            +lost_samples() uint32
            +stop() Result
        }
        class SampleRate {
            <<enumeration>>
            sps20
            sps45
            sps90
            sps175
            sps330
            sps600
            sps1000
        }
        class LoadCellSample {
            +int32 raw
            +uint64 timestamp_us
        }
        class Ads1220 {
            <<hal::zephyr>>
            -device* dev_
        }
        class IBatteryCharger {
            <<interface>>
            +init() Result
            +read() Result~BatteryStatus~
        }
        class BatteryStatus {
            +uint32 millivolts
            +bool charging
            +bool external_power
        }
        class IPowerOff {
            <<interface>>
            +init() Result
            +power_off() Result
        }
        class Npm13xxCharger {
            <<hal::zephyr>>
            Zephyr nPM13xx charger sensor
        }
        class RegulatorShipMode {
            <<hal::zephyr>>
            regulator_parent_ship_mode()
        }
        class IMotionSensor {
            <<interface>>
            +init() Result
            +moved() bool
        }
        class SensorMotion {
            <<hal::zephyr>>
            SENSOR_TRIG_MOTION, slope threshold
        }
        class GpioWakeSystemOff {
            <<hal::zephyr>>
            sys_poweroff(), woken by a GPIO
        }
    }

    namespace drivers {
        class ads1220_driver {
            <<C driver, jkp,ads1220>>
            ads1220_start(dev, sps) int
            ads1220_read(dev, sample*, timeout) int
            ads1220_lost_samples(dev) uint32
            ads1220_stop(dev) int
        }
        class ads1220_emul {
            <<native_sim only>>
            emulates the chip on the SPI emulator bus
            ads1220_emul_set_raw(raw)
            ads1220_emul_set_source(fn)
        }
        class lsm6dsl_driver {
            <<Zephyr st,lsm6dsl, patched>>
            SENSOR_TRIG_MOTION
            SENSOR_ATTR_SLOPE_TH, SLOPE_DUR
            lsm6dsl_emul on native_sim
        }
        class npm1300_emul {
            <<native_sim only>>
            nPM1300 registers on the I2C emulator bus
            npm1300_emul_set_battery_mv(mV)
            npm1300_emul_set_charging(bool)
            npm1300_emul_set_vbus(bool)
        }
    }

    class fw_settings {
        <<lib/fw>>
        init() Result
        load~T~(key) Result~T~
        save(key, value) Result
        erase(key) Result
    }

    IIdentity <|.. Identity
    ILoadCell <|.. LoadCell
    IClient <|.. Client
    ILoadCellAdc <|.. Ads1220
    IPmic <|.. Pmic
    IBatteryCharger <|.. Npm13xxCharger
    IPowerOff <|.. RegulatorShipMode
    IPowerOff <|.. GpioWakeSystemOff
    IMotion <|.. Motion
    ILed <|.. Led
    IMotionSensor <|.. SensorMotion

    main ..> IIdentity : identity()
    main ..> ILoadCell : load_cell()
    main ..> fw_settings : init
    main ..> Client : ble_svc init()
    main ..> Measurement : measurement_manager start()
    main ..> IPmic : pmic() init
    main ..> Power : power_manager start()
    main ..> IMotion : motion() init
    main ..> ILed : led() init
    debug_shell ..> IIdentity
    debug_shell ..> ILoadCell : rate only
    debug_shell ..> battery : reads

    Identity ..> fw_settings : "identity/id"
    Identity ..> DeviceId
    LoadCell --> ILoadCellAdc : adc_
    LoadCell ..> calibration
    LoadCell ..> fw_settings : "load_cell/factor", "load_cell/rate"
    LoadCell ..> ForceSample
    ILoadCellAdc ..> SampleRate
    ILoadCellAdc ..> LoadCellSample
    Measurement --> ILoadCell : load_cell_
    Measurement --> IClient : client_
    Measurement ..> progressor : encode responses and samples
    Measurement ..> Event
    Measurement ..> battery : reads for 0x6F
    Power --> IPmic : pmic_
    Power --> IClient : low-power warning
    Power ..> battery : publishes
    Indicator --> ILed : led_
    Activity --> Indicator : off while asleep
    battery ..> Indicator : listener, queue
    Activity --> IMotion : moved, sleep_until_moved
    Activity --> IClient : disconnect when idle
    progressor_command ..> Activity : listener, queue
    client_connection ..> Activity : listener, queue
    measurement_state ..> Activity : listener, queue
    Measurement ..> measurement_state : publishes
    Motion --> IMotionSensor : sensor_
    Motion --> IPowerOff : system_off_
    SensorMotion ..> lsm6dsl_driver
    Pmic --> IBatteryCharger : charger_
    Pmic --> IPowerOff : power_off_
    IBatteryCharger ..> BatteryStatus
    npm1300_emul ..> Npm13xxCharger : answers Zephyr's drivers over I2C
    progressor_command ..> Event : listener, queue
    client_connection ..> Event : listener, queue
    Client ..> progressor : decode_command
    Client ..> progressor_command : publishes
    Client ..> client_connection : publishes
    Client ..> IIdentity : name from id()
    JkpService ..> jkp_protocol : check writes, encode values
    JkpService ..> config_command : publishes
    JkpService ..> IIdentity : id()
    JkpService ..> ILoadCell : rate only
    calibration_state ..> JkpService : listener, notify
    config_command ..> Event : listener, queue
    config_command ..> Activity : listener, queue
    Measurement ..> calibration_state : publishes
    battery ..> Client : listener, Battery service level
    Ads1220 ..> ads1220_driver
    ads1220_emul ..> ads1220_driver : answers its SPI
```

The measurement manager's thread takes `Event`s off its queue (fed by a zbus listener on its three channels) and, while measuring, calls `sample()` between them: each `ForceSample` becomes a `progressor::Sample` with its time since the measurement started, and batches of up to 50 ms (at most what one notification carries) go out through `ble_svc::client()`. It also applies configuration requests from the JustKeepPulling service (`channels::config_command`: a calibration point, a calibration reset, the sample rate) and publishes how calibration went on `channels::calibration_state`, which the service reads and notifies. It is the only caller of `ILoadCell`'s single-threaded operations; the JustKeepPulling service and the debug shell only read the thread-safe sample rate.

The power manager's thread reads the battery every 30 s (its timer posts an `Event`) and publishes it on `channels::battery`, which the measurement manager answers 0x6F from. A reading at or below 3.3 V with no charging or USB power is confirmed 1 s later; then the client gets the 0x04 low-power warning and, 0.5 s later, the PMIC enters ship mode. It is the only caller of `IPmic`.

The same thread tracks inactivity (`Activity`): a 10 s timer checks for motion and counts idle time, and commands (except battery polls), configuration requests, connections and measurements reset it; nothing counts while measuring. After 4 minutes idle (30 with a client connected), the client is disconnected and, 10 s later unless something happened meanwhile, the SoC enters System OFF, woken by the IMU's motion interrupt.

It also drives the status LED (`Indicator`), as Crimpdeq: blue without a client, green with one, red blinking (250 ms on, 750 ms off, on a 250 ms timer) at or below 3.5 V, and off before sleeping, since GPIOs keep their state in System OFF.

## Production wiring

Each component's production file binds the concrete types (see `docs/architecture.md`, Component pattern):

| Accessor | Instance | Bound to |
|---|---|---|
| `identity_svc::identity()` | `Identity` | settings key `identity/id` |
| `measurement_manager::start()` | `Measurement` in `measurement_manager.cpp`, built on first use | `load_cell()`, `client()`, and the device ID as at boot; thread priority 2 |
| `power_manager::start()` | `Power`, `Activity` and `Indicator` in `power_manager.cpp`, built on first use | `pmic()`, `motion()`, `led()`, `client()`; idle timeouts from Kconfig; thread priority 7 |
| `motion_svc::motion()` | `Motion` on `hal::zephyr::SensorMotion` and `GpioWakeSystemOff` | devicetree alias `motion-sensor`: the XIAO's LSM6DS3TR-C (Zephyr's LSM6DSL driver, patched; deferred init after the PMIC's LDO1), its INT1 (P0.06) waking the SoC; or the emulated chip on native_sim |
| `led_svc::led()` | `Led` on three `hal::zephyr::GpioOutput`s | devicetree aliases `status-led-red`, `-green`, `-blue`: the XIAO's RGB LED (active low; red P1.22, green P1.24, blue P1.23, as Seeed's current board files), or emulated GPIOs on native_sim |
| `pmic_svc::pmic()` | `Pmic` on `hal::zephyr::Npm13xxCharger` and `RegulatorShipMode` | devicetree aliases `pmic-charger` and `pmic-regulators`: the XIAO's nPM1300 (bit-banged I2C on P1.18/P1.17), or the emulated chip on native_sim |
| `ble_svc::client()` | `Client` in `ble_svc.cpp` | Bluetooth: the Progressor GATT service, advertising as `Progressor_NNNN` (NNNN from the device ID) |
| (none: `jkp_svc` has no API) | `BT_GATT_SERVICE_DEFINE` in `jkp_svc.cpp` | Bluetooth: the JustKeepPulling GATT service, beside the Progressor one; reads `identity()` and `load_cell()`'s sample rate |
| `load_cell_svc::load_cell()` | `LoadCell` on `hal::zephyr::Ads1220` | devicetree alias `load-cell-adc`: the ADS1220 on the XIAO's SPI (`/CS` P1.00, `/DRDY` P1.31), or the emulated chip on native_sim |
