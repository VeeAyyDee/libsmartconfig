SPDX-License-Identifier: 0BSD

ESP-IDF provisioning example
============================

This example uses the original sc_touch/sc_touch2/sc_airkiss APIs in this
component, instead of calling the vendor esp_smartconfig workflow. It targets
ESP32-S3 with ESP-IDF >=6.0,<7.0. The component still uses the SDK Wi-Fi/PHY
radio and network stack; it does not replace those blobs.

Build from this directory in an activated ESP-IDF environment:

  idf.py set-target esp32s3
  idf.py menuconfig
  idf.py build

In "Clean-room provisioning example", select one protocol:
  CONFIG_SC_EXAMPLE_PROTOCOL_V2       ESP-Touch v2 (default)
  CONFIG_SC_EXAMPLE_PROTOCOL_V1       ESP-Touch v1
  CONFIG_SC_EXAMPLE_PROTOCOL_AIRKISS  AirKiss full-message mode

The project output is cleanroom_provision_example.elf. The example locates
the package root through ../.. and derives its component name from that
folder; both libsmartconfig and a renamed sc_provision directory work.
There are no remote component dependencies or machine-specific paths.
The project builds main and its declared dependency closure, rather than
enabling unrelated optional SDK components.

ESP-Touch v2 optionally accepts CONFIG_SC_EXAMPLE_V2_KEY: exactly 32
hexadecimal digits, or empty for plaintext. Invalid lengths/characters are
rejected without logging the key. A key in sdkconfig is also embedded in
firmware; this is not secure key storage. Use a separate key-management
design for production. The public PSA Crypto wrapper supplies CBC decryption;
no private AES implementation is included here.

If you choose to flash your own board, run for your actual serial port:

  idf.py -p PORT flash monitor

Flashing is a user operation. Packaging/build checks do not flash hardware.
Earlier controlled protocol hardware tests used separate harnesses. This
new example has not itself been hardware tested; do not transfer those
earlier interoperability claims to it.

Execution and limits
---------------------
The example initializes NVS, a default event loop/netif and STA Wi-Fi. NVS
errors are reported without automatic erase. PHY calibration storage and
Wi-Fi NVS defaults stay enabled; decoded Wi-Fi configuration is explicitly
RAM-only. Credentials are not persisted by this example. It also suppresses
SDK Wi-Fi info logs, which can otherwise include the SSID in association
messages; its own logs contain only progress, errors, IP and completion.

Event callbacks copy fixed-size data into a bounded static FreeRTOS queue.
Queue-send failures abort the session. The application task calls the chosen
adapter start/poll/stop functions, polls every 10 ms, and owns association,
retries and sockets. Provisioning has a 120-second deadline; association/DHCP
has a 30-second deadline and at most three connect attempts. Retryable event
post errors are polled again within the deadline. Stop/partial-start rollback
errors are retried at most five times. Association never begins after failed
capture cleanup.

Capture is stopped before setting the Wi-Fi configuration or connecting.
The example uses decoded BSSID for v1, and captured BSSID for v2/AirKiss.
It never sets the association channel from lock.channel: that field reports
the observed capture channel, which may differ from AP primary channel.
Empty SSIDs and embedded NUL credential bytes are rejected at this Wi-Fi
boundary, even though the portable byte codecs can represent them. Counted
arrays are copied into a zeroed wifi_config_t with bounds checked first.

This demo uses IPv4 DHCP and IPv4 UDP only. It explicitly rejects v2 messages
whose IPv4 flag is zero. Reserved v2 data is decoded but not logged or used.
Exactly one session runs per boot. Success leaves the station connected;
failure tears down the example's Wi-Fi state. Reboot to try a new session.

After DHCP, the existing formatter builds the reply:
  v1:      11 bytes, unicast to the decoded sender IPv4 address, UDP 18266.
           Local IPv4 bytes are copied in network order.
  v2:       7 bytes, broadcast UDP 18266+10000*port_mark.
  AirKiss:  1 token byte, broadcast UDP 10000.

The socket is nonblocking. At most 30 datagrams are attempted 100 ms apart;
errors and close failures are checked. Wi-Fi loss or event-queue overflow
aborts sending. A successful send means local socket acceptance only; it is
not confirmation that a phone received the reply. The example does not
implement vendor phone discovery, an identical vendor ACK lifecycle, or a
full vendor API/ABI compatibility layer.

Mapping the workflow
---------------------
Replace vendor provisioning start/stop calls with exactly one of:
  sc_touch_idf_start/poll/stop
  sc_touch2_idf_start/poll/stop
  sc_airkiss_idf_start/poll/stop

Use the matching SC_TOUCH_EVENT, SC_TOUCH2_EVENT or SC_AIRKISS_EVENT base.
FOUND_CHANNEL supplies sc_capture_lock; GOT_CREDENTIALS supplies that
protocol's counted result struct. The application remains responsible for
Wi-Fi initialization, event handling, stopping capture, connecting, and using
the formatter/socket for acknowledgments. Stop one adapter before starting
another, including after completed reception. No esp_smartconfig symbols are
provided or called by this example.
