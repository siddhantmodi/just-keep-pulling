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

## Helpers

- `connect_by_name(central, name)`: scan for the advertised name and connect.
- `wait_for_log(dut, regex)`: wait for a line of app output without blocking
  Bumble's event loop.
- `eventually(read, expected)`: retry a read until it returns the expected value.

## Notes

- The app's Bluetooth controller is Bumble's, with no public address, so
  Zephyr uses a static random identity, as on Nordic hardware. Find it by
  scanning rather than by a fixed address.
- Set `CONFIG_BT_HCI_ACL_FLOW_CONTROL=n` in the app's native_sim config:
  Bumble's controller does not support host ACL flow control.
