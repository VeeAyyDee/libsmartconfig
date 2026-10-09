SPDX-License-Identifier: 0BSD

libsmartconfig 0.2.0: standard ESP-IDF application workflow
========================================================
This is the default application example. It includes esp_smartconfig.h and
uses SC_EVENT, esp_smartconfig_set_type/start/stop, then ordinary Wi-Fi
association. No sc_* application calls or receive polling are required.
The SDK's existing open-source wrapper owns the acknowledgment task and
SC_EVENT_SEND_ACK_DONE. This component replaces its receiver archive only.

Build in an activated ESP-IDF 6 environment:
  idf.py set-target esp32s3
  idf.py menuconfig
  idf.py build

The ELF stem is libsmartconfig_example. Configuration:
  CONFIG_LIBSMARTCONFIG_EXAMPLE_TYPE: 0 ESPTouch, 1 AirKiss,
                                     2 simultaneous ESPTouch/AirKiss (default),
                                     3 ESPTouch v2.
  CONFIG_LIBSMARTCONFIG_EXAMPLE_V2_KEY: empty for plaintext, or exactly
                                     32 hex digits decoded to 16 raw bytes.
A demo key in sdkconfig is embedded in firmware, not secure storage.
The public SDK start config has no AirKiss key parameter. For explicitly
keyed AirKiss, retain the optional examples/provision extension workflow.

The top CMake calls libsmartconfig_replace_sdk(component_folder_name) AFTER
project(), replacing the SDK imported esp_wifi_smartconfig archive location.
Main explicitly requires this component. Its emitted archive is
libsmartconfig.a, with no second proprietary SmartConfig archive. Folder
renaming works because CMake derives the component folder name locally.
The hook requires IDF major6 and the expected imported target; target builds
must be validated against the actual SDK. No absolute workstation path is
stored in source. Wi-Fi/PHY and the open SDK wrapper/ACK code remain SDK code.

Audit a linked standard example with:
  python3 ../../tools/check_no_smartconfig_blob.py --protocol standard \
    --map build/libsmartconfig_example.map --elf build/libsmartconfig_example.elf \
    --replacement-archive build/esp-idf/COMPONENT_FOLDER/libsmartconfig.a

Use the actual component folder and toolchain nm (or --nm PATH). The audit
accepts members of that exact built archive, rejects other libsmartconfig
archive paths and requires wrapper plus original internal receiver symbols.
A renamed archive alone is not proof of source provenance; preserve build
inputs and inspect the full map when validating a release.

Behavior and scope
------------------
Credentials stay in RAM. NVS initialization errors do not trigger erase.
Wi-Fi informational logs are suppressed to avoid SDK SSID logging; neither
this example nor the receiver logs credentials or keys. The credential event
handler configures/starts association and retries up to three attempts. The
SDK ACK_DONE handler stops SmartConfig. An application task imposes a
180-second overall deadline and retries cleanup up to five times on failure.
One session runs per boot, and success keeps the station connected.

The receiver itself neither connects nor sends acknowledgment datagrams.
Its fixed ring/worker owns capture, posts FOUND_CHANNEL before the single
credentials event, and disables reception before credentials are exposed.
Handlers may call stop; event posts never block waiting for those handlers.
Already queued events are SDK-owned copies and may outlive a stop call.
Do not destroy handler context until unregistering/draining your event flow.

This is bounded public-workflow compatibility, not every proprietary internal
ABI or heuristic. Fast mode true returns ESP_ERR_NOT_SUPPORTED. There is no
AP scan/SSID reconstruction, and no fabricated SCAN_DONE event. Full-message
SSID is required; empty/embedded-NUL credentials are rejected at the standard
string-event boundary. Full 32/64-byte arrays may have no terminating byte,
matching the fixed Wi-Fi field capacity; applications must use bounded copies.
Standard v2 currently requires IPv4. APSTA capture stays on its existing AP
channel and requires disconnected STA, so sender must be heard there. Normal
STA hops regulatory channels. Capture APIs are mutually exclusive with any
other promiscuous owner; stop one receiver before using another.

The optional sc_* portable byte APIs still accept their documented binary
policy. Keyed AirKiss is preserved as an explicit optional API extension.
Refer to README.txt and PROVENANCE.json for individual tests and evidence
limits. Host mocks, SDK builds and controlled hardware are separate claims.
