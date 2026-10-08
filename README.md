# SC Provision

Experimental ESP-IDF component for ESP-Touch v1, ESP-Touch v2 and full-message
cleartext AirKiss reception on ESP32-S3. The portable C11 decoders can also be
used without ESP-IDF. Released under [0BSD](LICENSE) as freely reusable code.

This replaces the SmartConfig provisioning receiver with original source and
an original API. Espressif's Wi-Fi and PHY still provide radio reception and
network connectivity. This is **not a blob-free Wi-Fi stack** or a drop-in
replacement for `esp_smartconfig_*`.

## Status

| Feature | Included |
| --- | --- |
| ESP-Touch v1 | Decoder, capture adapter, IDF polling adapter, ACK formatter |
| ESP-Touch v2 | Plaintext and AES-CBC security1/security2, reserved data, PSA Crypto adapter |
| AirKiss | Full-message plaintext or keyed AES-128-CBC reception, PSA adapter and basic token ACK |
| ESP-IDF component | ESP32-S3, IDF 6.x; build validation uses IDF 6.0.3 |
| Android sender app | [Test app](android/README.md): v1, all v2 modes, plaintext/encrypted AirKiss; tested on Android 11 with two S3 boards |

Controlled two-board comparisons against the original receiver passed for
v1, plaintext AirKiss and all three v2 modes. Host tests cover framing, CRCs, bounds,
loss/repetition, capture state and lifecycle errors. The new Android sender also
passed all six provisioning modes on a POCO X3 NFC running Android 11, through
an ESP32-S3 WPA2 AP. This is one phone/AP setup, not general Android/AP coverage;
see the [phone validation record](android/VALIDATION.md).
See [validation and limitations](docs/VALIDATION.md) for the precise scope.

Version 0.1.1 adds encrypted AirKiss. See the [behavioral contract](spec/AIRKISS-ENCRYPTED-01.txt)
and validation record for its test coverage. Three encrypted credential cases,
an open-network case and a wrong-key negative passed on two ESP32-S3 boards;
positive cases completed association, DHCP, UDP echo and token acknowledgement. The no-argument AirKiss APIs retain
plaintext behavior; use `sc_airkiss_idf_start_with_key(key, key_len)` for keyed
reception, or set the example's AirKiss key to the hexadecimal encoding of the
sender's raw key bytes. The key is not inferred from the incoming message.

## Build the example

Install ESP-IDF 6.x with the ESP32-S3 toolchain and activate its environment.
From this repository:

```sh
cd examples/provision
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
```

The example defaults to ESP-Touch v2 plaintext. Its configuration menu also
selects v1, AirKiss and separate v2/AirKiss AES key settings. It receives credentials, stops the decoder,
joins the network, waits for DHCP and sends the selected acknowledgement.
Credentials are kept in RAM for this one-session demo; PHY calibration uses NVS.

To install the demo on your own board, after backing up any firmware/data you
need, choose that board's port:

```sh
idf.py -p /dev/ttyUSB0 flash monitor
```

Flashing installs this example application. It does not preserve an existing
application automatically. [Example configuration and lifecycle](examples/provision/README.txt).

## Add the component to your application

Copy this repository into `your-project/components/sc_provision`, or add its
directory to `EXTRA_COMPONENT_DIRS` before the project declaration. Add
`sc_provision` to your application's `REQUIRES` list when using that folder name.
The included example also works when this repository directory is renamed.

Use the protocol-specific public headers:

| Operation | ESP-Touch v1 | ESP-Touch v2 | AirKiss |
| --- | --- | --- | --- |
| Header | `sc_touch_idf.h` | `sc_touch2_idf.h` | `sc_airkiss_idf.h` |
| Start | `sc_touch_idf_start()` | `sc_touch2_idf_start(key_or_null)` | `sc_airkiss_idf_start()` |
| Poll | `sc_touch_idf_poll()` | `sc_touch2_idf_poll()` | `sc_airkiss_idf_poll()` |
| Stop | `sc_touch_idf_stop()` | `sc_touch2_idf_stop()` | `sc_airkiss_idf_stop()` |
| Event base | `SC_TOUCH_EVENT` | `SC_TOUCH2_EVENT` | `SC_AIRKISS_EVENT` |

Initialize Wi-Fi in disconnected STA mode and create the default event loop
first. Run only one adapter at a time, poll about every 10 ms, copy event data
before the callback returns, and stop the adapter before another session.
The example demonstrates association and sockets; the component itself does
not store credentials, associate or manage a phone app.

Existing applications must replace their vendor start/stop/event workflow.
Merely linking this component does not redirect `esp_smartconfig_start()`.
Do not include/call `esp_smartconfig.h` APIs in the new flow.

## Check that the SmartConfig blob is absent

After building the example, from its directory, run the included audit with
the generated map/ELF and the selected protocol (`v1`, `v2` or `airkiss`):

```sh
python3 ../../tools/check_no_smartconfig_blob.py \
  --map build/*.map --elf build/*.elf --protocol v2
```

It rejects linked `libsmartconfig.a` members and defined `esp_smartconfig_*`
symbols, and requires our adapter's start/poll/stop symbols. It does not claim
that other Wi-Fi, PHY or Bluetooth blobs have been removed.

## Portable builds and tests

```sh
make all test test-idf
make sanitize sanitize-idf
```

The host build produces `libsc_touch.a` and `libsc_touch.so` containing all
three portable decoders/capture adapters. IDF lifecycle/PSA tests use owned
mocks. They are not actual-radio or cryptographic conformance tests.

In traced environments that cannot run LeakSanitizer, use
`ASAN_OPTIONS=detect_leaks=0` and report that leak detection was disabled.

## Boundaries and provenance

The implementation was written from recorded behavioral specifications by a
separate implementation agent. The supervisor performed independent checks
and controlled hardware comparisons. This was procedural separation on a
shared filesystem, not enforced isolation or legal certification.

- [Detailed API and behavior reference](README.txt)
- [Development provenance](PROVENANCE.json)
- [Protocol contracts](spec/)
- [License scope](LICENSING.txt)
- [Android sender handoff](docs/ANDROID-HANDOFF.md)
- [Contribution and testing guidance](CONTRIBUTING.md)

CRC fields, AirKiss tokens and AES-CBC padding do not authenticate a device or
sender. Keyed AirKiss is explicitly selected by the caller and uses the protocol's
key-as-IV AES-CBC convention; it provides no authentication. Scan-assisted SSID
recovery and the complete vendor discovery lifecycle are not implemented. One v2 lab run reported capture channel
10 before association on AP primary channel 6 (40U); its cause is unproven.
Treat the capture channel as an observation and let association find the AP.

No third-party sender source, proprietary archives, device backups, firmware
images or private network credentials are included in this source repository.
