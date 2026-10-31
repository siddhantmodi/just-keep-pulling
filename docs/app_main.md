# app_main design

How `app_main`'s interfaces, classes, modules and drivers fit together, and what each offers. Keep this diagram in step with the code: a change to an interface or a new component updates it in the same commit.

Conventions: `<<interface>>` classes are the seams that tests mock; a solid arrow is a reference held by an object, a dashed arrow is a call or use. Methods return `fw::Result` (`Result~T~` here) unless shown otherwise. Mocks (`hal::mocks::MockLoadCellAdc`, `load_cell_svc::MockLoadCell`, `ble_svc::MockClient`) implement each interface for unit tests and are left out to keep the diagram readable.

```mermaid
classDiagram
    direction TB

    class main {
        <<entry point>>
        main()
    }

    class debug_shell {
        <<debug builds only>>
        jkp id [set hex]
        jkp rate [set SPS]
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

    main ..> IIdentity : identity()
    main ..> ILoadCell : load_cell()
    main ..> fw_settings : init
    main ..> Client : ble_svc init()
    debug_shell ..> IIdentity
    debug_shell ..> ILoadCell : rate only

    Identity ..> fw_settings : "identity/id"
    Identity ..> DeviceId
    LoadCell --> ILoadCellAdc : adc_
    LoadCell ..> calibration
    LoadCell ..> fw_settings : "load_cell/factor", "load_cell/rate"
    LoadCell ..> ForceSample
    ILoadCellAdc ..> SampleRate
    ILoadCellAdc ..> LoadCellSample
    Client ..> progressor : decode_command
    Client ..> progressor_command : publishes
    Client ..> client_connection : publishes
    Client ..> IIdentity : name from id()
    Ads1220 ..> ads1220_driver
    ads1220_emul ..> ads1220_driver : answers its SPI
```

Nothing observes the channels yet: the measurement manager (next) handles the commands, turns `ForceSample`s into `progressor` packets, and answers through `ble_svc::client()`.

## Production wiring

Each component's production file binds the concrete types (see `docs/architecture.md`, Component pattern):

| Accessor | Instance | Bound to |
|---|---|---|
| `identity_svc::identity()` | `Identity` | settings key `identity/id` |
| `ble_svc::client()` | `Client` in `ble_svc.cpp` | Bluetooth: the Progressor GATT service, advertising as `Progressor_NNNN` (NNNN from the device ID) |
| `load_cell_svc::load_cell()` | `LoadCell` on `hal::zephyr::Ads1220` | devicetree alias `load-cell-adc`: the ADS1220 on the XIAO's SPI (`/CS` P1.00, `/DRDY` P1.31), or the emulated chip on native_sim |
