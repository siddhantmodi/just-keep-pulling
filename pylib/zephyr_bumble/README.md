# zephyr-bumble

pytest fixtures for testing a Zephyr BLE app built for `native_sim` against a
[Bumble](https://github.com/google/bumble) virtual central, under Twister's
`harness: pytest`. Nothing in it is specific to one app or project.

```
Bumble central ── controller ══ LocalLink (virtual radio) ══ controller ── TCP (HCI H4) ── zephyr.exe --bt-dev=127.0.0.1:<port>
```

## Use

Install it (`pip install -e pylib/zephyr_bumble`); pytest loads the plugin
automatically. In a Twister test case:

```yaml
tests:
  my_app.integration:
    harness: pytest
    harness_config:
      pytest_root: ["integration/test_ble.py"]
    platform_allow: [native_sim/native/64]
```

```python
import pytest
from zephyr_bumble import connect_by_name, wait_for_log

@pytest.mark.asyncio
async def test_connects(ble_dut, virtual_link):
    connection = await connect_by_name(virtual_link.central, "My Device")
    await wait_for_log(ble_dut, r"connected")
```

## Fixtures

- `virtual_link`: a `VirtualLink`: the virtual radio, the TCP HCI server for
  the app, and `virtual_link.central`, a powered-on Bumble `Device`.
- `ble_dut`: Twister's device under test, launched with `--bt-dev` pointing at
  `virtual_link`. Its output goes to Twister's logs.
- `dut_args`: extra `zephyr.exe` arguments for `ble_dut` (empty by default).
  Override it in a conftest, e.g. `["--device_id=0x5eed1d00"]` to fix the
  `hwinfo` device ID the app reads.

## Helpers

- `find_advertiser(central, name=None, *, service_uuid=None)`: scan until a
  device advertises the name and/or the service UUID; raises `TimeoutError` if
  none does, which is also how to check advertising stopped.
- `connect_by_name(central, name)` / `connect_by_service(central, service_uuid)`:
  scan for the advertised name or service UUID and connect.
- `wait_for_log(dut, regex)`: wait for a line of app output without blocking
  Bumble's event loop.
- `eventually(read, expected)`: retry a read until it returns the expected value.

## Notes

- The app's Bluetooth controller is Bumble's, with no public address, so
  Zephyr uses a static random identity, as on Nordic hardware. Find it by
  scanning rather than by a fixed address.
- Bumble's virtual controller reports the advertising data again as the scan
  response, so a name or UUID the app only puts in its scan response is never
  seen. Match on something in the advertising data.
- Set `CONFIG_BT_HCI_ACL_FLOW_CONTROL=n` in the app's native_sim config:
  Bumble's controller does not support host ACL flow control.
