# humsienk-esp32

Read **HumsiENK battery BMS data from an ESP32 over Bluetooth**: SOC, pack
voltage/current, individual cells, temperatures and capacity. No cloud login,
Wi-Fi connection or inverter required. Read-only: no charging settings, MOS
switches, protection changes or firmware updates.

## Supported battery

Supports **HS04 / WATT protocol version 4**, used by the tested
16-cell, 51.2 V / 300 Ah HumsiENK battery and the
[HumsiENK Smart BMS app](https://play.google.com/store/apps/details?id=uni.UNI3890CA7).
Other HumsiENK batteries may use different protocols and are not necessarily compatible.

<a href="https://eu.humsienk.com/products/48v-300ah-wall-mounted-lifepo4-battery">
  <img src="https://eu.humsienk.com/cdn/shop/files/Humsienk-48V-300Ah-Wall-Mounted-Bluetooth-LiFePO4-Battery.png?v=1775810412&width=720" alt="HumsiENK 51.2 V 300 Ah wall-mounted battery — manufacturer product image" width="320">
</a>

[Manufacturer's 51.2 V 300 Ah battery page](https://eu.humsienk.com/products/48v-300ah-wall-mounted-lifepo4-battery).
The photo illustrates the battery family, not a confirmed exact model match.
Image © HumsiENK; **not covered by this repository's MIT license**.
This is an independent project, not an official HumsiENK library.

## Hardware testing

Tested with real HumsiENK battery hardware and an ESP32-C3, including a
continuous 24-hour BLE session with one-minute readings and concurrent Wi-Fi.
Compatibility with other models and long-term reliability are not guaranteed.
Not intended for safety-critical use.

## Install / build

Reference configuration: **Arduino-ESP32 2.0.17**, ESP32-C3,
**NimBLE-Arduino 2.3.6**. The included PlatformIO configuration pins these
dependencies. Other cores/chips are not yet qualified; ESP32-S2 has no BLE.

For a local PlatformIO project:

```ini
lib_deps =
    humsienk-esp32=symlink:///absolute/path/to/humsienk-esp32
```

Or copy this library into your Arduino `libraries` directory and install
NimBLE-Arduino 2.3.6.
This project is not yet published to the Arduino/PlatformIO registries.

Build the bundled discovery/read example:

```sh
pio run -e esp32c3
```

## Discover and read

```cpp
#include <humsienk/esp32.h>

auto& bms = humsienk::battery();

void setup() {
  Serial.begin(115200);
  auto error = bms.begin();
  if (error != humsienk::Error::Ok) {
    Serial.println(humsienk::errorName(error));
    return;
  }
  humsienk::Candidate candidates[8];
  size_t count = 0;
  error = bms.discover(candidates, 8, count);
  if (error != humsienk::Error::Ok || count != 1) {
    Serial.println("Select exactly one battery; see ReadBattery example.");
    return;
  }
  error = bms.connect(candidates[0].target);
  if (error != humsienk::Error::Ok)
    Serial.println(humsienk::errorName(error));
}

void loop() {
  if (bms.connected()) {
    humsienk::Snapshot data;
    const auto error = bms.readSnapshot(data);
    if (error == humsienk::Error::Ok) {
      Serial.printf("SOC: %u%%\n", data.socPercent);
      Serial.printf("Battery: %.2f V, %+.1f A\n", data.voltageV, data.currentA);
    } else Serial.println(humsienk::errorName(error));
  }
  delay(10000);
}
```

The short example stops reading after a connection failure. The full
[ReadBattery example](examples/ReadBattery/ReadBattery.ino) also retries the
saved target, lists candidates and prints cells and temperatures. Discovery
returns *candidates*, not proven compatible devices; `connect()` validates GATT,
handshake, product response version and CRC. Scan storage is capped at 32
advertisements, so a crowded scan can miss a battery; repeat if necessary.
The example refuses to silently choose among multiple candidates.

## More readings, without register numbers

After a successful `readSnapshot(data)`:

```cpp
Serial.printf("Remaining: %.1f Ah / total field: %.1f Ah\n",
              data.remainingCapacityAh, data.totalCapacityAh);
Serial.printf("Design: %.1f Ah, cycles: %u\n", data.designCapacityAh, data.cycles);
Serial.printf("Cell range: %.3f–%.3f V\n", data.minCellV(), data.maxCellV());
for (unsigned i = 0; i < data.cellCount; ++i)
  Serial.printf("Cell %u: %u mV\n", i + 1, data.cellMillivolts[i]);
for (unsigned i = 0; i < data.temperatureCount; ++i)
  Serial.printf("Sensor %u: %.1f C\n", i + 1, data.temperaturesC[i]);
```

Positive current indicates charging; negative indicates discharging.
Sensor order is provisionally MOS, PCB, then cell sensors. Sensor roles and
capacity/cycle meanings may vary by BMS; verify them against your battery's app.

Product identity and raw warnings:

```cpp
humsienk::Product product;
if (bms.readProduct(product) == humsienk::Error::Ok)
  Serial.printf("Firmware: %s, serial: %s\n", product.firmware, product.serial);

humsienk::Response warnings;
if (bms.readRaw(humsienk::ReadPoint::Warnings, warnings) == humsienk::Error::Ok) {
  for (size_t i = 0; i < warnings.size; ++i)
    Serial.printf("%02x", warnings.bytes[i]);
  Serial.println();
}
```

Warning **bytes are not yet mapped to named faults**. The only available raw
read points are `Product`, `CollectionBoards`, `Analog`, and `Warnings`; callers
cannot use this API to supply arbitrary commands or configuration writes.

## Experimental fields

```cpp
if (data.experimental.present) {
  Serial.printf("Experimental SOH: %u%%\n", data.experimental.sohPercent);
  Serial.printf("Experimental balancing current: %.1f A\n",
                data.experimental.balanceCurrentA);
}
```

`experimental` also contains `cumulativeCapacityAh` and `remainingMinutes`.
These fields have unconfirmed meanings or scaling. `present` indicates available
data, **not a verified interpretation**. Compare them with your battery's app.
`data.raw` preserves the validated response for further analysis.
Unsupported protocol versions are rejected.

## Reconnection and ownership

Save **both the BLE address and address type** from `Candidate.target`, not
just the name or serial number. Pass the same `Target` to `connect()` after
disconnecting. See [SavedBattery](examples/SavedBattery/SavedBattery.ino) for
application-owned Preferences storage; the library itself does not use NVS.
An address does not wake a powered-off/non-connectable battery and may not be
permanent for every device. Keep the phone app disconnected while testing.

Successful reads keep the BLE connection open. The application decides when to
read again, reconnect or call `disconnect()`; there is no automatic polling task.
A timeout, malformed reply or decoding failure closes the session. Output
objects remain unchanged on error: **never treat an old snapshot as a new read**.
The protocol has no transaction sequence number; duplicate replies within one
healthy session cannot be perfectly distinguished. Errors force a new session.

One `battery()` singleton owns NimBLE and the scan/client for its lifetime.
Call it from one application task, never a BLE callback; calls are blocking.
Connect timeout is 10 s, response wait is 10 s, and requests are spaced by 1 s.
NimBLE service discovery/ATT calls additionally use the stack's timeouts; this
is not a hard real-time whole-operation deadline.

**Do not mix NimBLE and Bluedroid on one ESP32.** The pure
protocol core (`humsienk.h`, `Session`, `Transport`) can be used with another
transport, but that integration needs separate testing.

The library does not manage Wi-Fi. When Wi-Fi is active, modem sleep must be
enabled before `begin()`; otherwise it returns `Busy`. Another BLE stack owning
the controller can also cause `Busy`.

## Tests

```sh
bash tests/run.sh
python3 -m unittest discover -s tests -p 'test_*.py'
# Alternatively, when CMake is installed:
cmake -S . -B build -DHUMSIENK_SANITIZE=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Tests cover decoding, CRC validation, malformed responses, fragmentation,
timeouts and reconnection.

## References and attribution

- [HumsiENK Smart BMS](https://play.google.com/store/apps/details?id=uni.UNI3890CA7),
  version 1.2.4: protocol reference.
- [mdelandgraaf/humsienk-esp32](https://github.com/mdelandgraaf/humsienk-esp32):
  separate project documenting a different BMS protocol (BMC); not a dependency.
- [aiobmsble TDT driver](https://github.com/patman15/aiobmsble/blob/main/aiobmsble/bms/tdt_bms.py):
  useful related-protocol reference; not an ESP32 runtime dependency.
- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino/tree/2.3.6): BLE adapter
  dependency, separately licensed by its authors.

Prepared with AI assistance. Library source is MIT licensed;
third-party names, software and imagery retain their respective rights.
