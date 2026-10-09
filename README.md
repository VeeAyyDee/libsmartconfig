# libsmartconfig

Original C receivers for ESPTouch v1, ESPTouch v2 and AirKiss, packaged as an
ESP-IDF component. Version **0.3.0** extends the standard `esp_smartconfig.h` API
workflow: start, receive `SC_EVENT` events, connect, and stop after acknowledgement.
Existing applications do not need the earlier `sc_*` polling API.

New implementation code is **0BSD**. ESP-IDF's existing Apache-2.0 public wrapper,
headers and ACK sender retain their own license. Wi-Fi MAC and PHY remain SDK
dependencies; this is not a complete open-source Wi-Fi stack.

## Install in an existing application

Copy this repository to `components/libsmartconfig`. Ensure `libsmartconfig` is
included in the build (add it to `COMPONENTS` if that list is restricted, or to
your main component's `REQUIRES`). After your root `project(...)` call, add:

```cmake
include("${CMAKE_CURRENT_LIST_DIR}/components/libsmartconfig/cmake/replace_sdk_smartconfig.cmake")
libsmartconfig_replace_sdk(libsmartconfig)
```

This selects our built `libsmartconfig.a` for the SDK's imported SmartConfig
archive target. The SDK's public start/stop wrapper, `SC_EVENT` definition and
ACK sender stay in place. **Application headers, calls and event handlers remain
unchanged.** A build change is required; merely copying an extra archive does
not reliably replace the vendor one.

The included [standard example](examples/standard/README.txt) uses this workflow:

```sh
cd examples/standard
idf.py set-target esp32s3
idf.py build
```

The supervisor also built the unmodified ESP-IDF 6.0.3 SmartConfig example's
application source and checked the link map: our receiver was selected, with no
members from the vendor SmartConfig archive. See [validation](docs/VALIDATION.md)
for hardware results and their scope.

## Compatibility scope

The compatibility layer supports single ESPTouch, single plaintext AirKiss,
combined ESPTouch/AirKiss, and plaintext or keyed ESPTouch v2. It owns the capture
worker; applications do not poll. Credential delivery uses the standard event
structure. The SDK sends ACKs after the application connects and gets an IP.
Start/stop, rollback, event delivery, timeout restart and reserved-data retrieval
have host tests. Target integration is tested on **ESP32-S3 / ESP-IDF 6.0.3**.

This is a source/API-compatible replacement for the tested workflows, **not a
claim of complete vendor behavior equivalence**:

- Discovery uses public asynchronous scans, two successful passes, a bounded
  64-BSSID cache and discovered channels within the country range. Empty or
  weak-only scans keep discovering. `SC_EVENT_SCAN_DONE` follows real discovery
  and capture enablement; timeouts start a fresh discovery attempt.
- Untouched channel dwell is 150ms. Explicit `fast_mode(false)` requests 100ms;
  `fast_mode(true)` requests 50ms. The option persists across sessions. Set it
  while inactive; active setters remain deliberately rejected.
- ESPTouch v1 and AirKiss acquire from one sliding quartet of distinct
  consecutive lengths in any order or phase. Their previous eight-packet gate
  could starve during50ms channel visits. Partial quartets do not hold a channel;
  original weak-signal FromDS survey preferences remain outside this scope.
- Start disconnects an associated STA and supports channel hopping in APSTA.
  Applications must suspend automatic reconnect and unrelated scans while
  provisioning. APSTA client service can be disrupted; driver retune errors
  are retried and reported when logging is enabled.
- Scan-assisted omitted SSIDs are supported for v1, plaintext AirKiss and v2,
  including encrypted v2 password/reserved fields. Recovery requires an exact
  locked BSSID, channel and protected-frame match plus protocol length/CRC
  checks. Hidden SSIDs, conflicting duplicate records and ambiguous identities
  are not guessed. Broader vendor cross-band/HT40 heuristics are unimplemented.
- The receiver needs exclusive promiscuous reception. Stop it before using the
  optional protocol-specific adapters or another promiscuous consumer.
- The standard SDK start structure has no AirKiss encryption key field. Keyed
  AirKiss remains available through the optional explicit-key API below.
- Phone interoperability, all AP/cipher combinations and sustained concurrent
  application workloads are not established by controlled tests.

## Optional portable and keyed interfaces

The existing `sc_touch_*`, `sc_touch2_*` and `sc_airkiss_*` decoders remain for
applications needing direct capture or portable C integration. They are optional,
not names the standard SmartConfig application must migrate to. The earlier
[protocol example](examples/provision/README.txt) demonstrates that interface.

Encrypted AirKiss uses `sc_airkiss_idf_start_with_key(key, key_len)` or the keyed
portable configuration. Keys are explicit 1..16 raw bytes; the protocol uses
AES-128-CBC with its key-as-IV convention. Do not interpret CRCs, padding or the
AirKiss token as sender authentication. Standard ESPTouch v2 encryption uses the
existing `esp_touch_v2_enable_crypt` and `esp_touch_v2_key` fields.

## Tests and provenance

The [Android test app](android/README.md) supports ESP-Touch v1, all v2 modes,
and plaintext/encrypted AirKiss. All six modes passed on a POCO X3 NFC running
Android 11 with two ESP32-S3 boards and ESP-IDF 5.5.2 using the optional polling
example. This historical phone evidence does not validate the new standard API
worker; see the [phone validation record](android/VALIDATION.md).

See [README.txt](README.txt) for commands and API details, [validation](docs/VALIDATION.md),
[protocol contracts](spec/), [provenance](PROVENANCE.json), and
[license scope](LICENSING.txt). Implementation used a separate specification-fed
agent on a shared filesystem; this is procedural separation, not legal certification.

No proprietary archives, device backups, private network credentials or firmware
images belong in this source release. Nothing is published automatically.
