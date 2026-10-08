# Android sender app handoff

Build a small, usable Android sender for this receiver component. Start with
ESP-Touch v2 plaintext, then add security version 2 with a user-supplied key.
Use the repository's behavioral specifications and public Android APIs. Keep
the encoder separate from the UI and socket transport so it can be tested
without a phone. No Android app or APK is included in this release.

## Read first

- [ESP-Touch v2 wire contract](../spec/ESPTOUCH-V2.txt)
- [Receiver API and limits](../README.txt)
- [Firmware example](../examples/provision/README.txt)
- [Validation summary](VALIDATION.md)

The specifications describe observable packet formats and our receiver policy.
They are not proprietary receiver source. Do not import a third-party sender
implementation without checking its license. New project code uses 0BSD;
dependencies retain their own terms.

## First working flow

1. The user connects the phone to the intended 2.4 GHz Wi-Fi network, enters
   its SSID/password, and checks the selected network/BSSID. The S3 must be
   able to hear that network's radio traffic. A phone on a different band of
   the same SSID is not a verified setup.
2. Obtain the permissions required by the chosen APIs and Android version.
   Explain denials and allow retry without restarting the app.
3. Open the acknowledgement listener before sending. Use one selected Wi-Fi
   network for the session; do not let mobile data or a VPN silently become
   the provisioning route.
4. Encode counted UTF-8 bytes, enforce byte limits, and send paced UDP
   datagrams whose payload lengths match the contract. Repeat the guide,
   header and complete body within a bounded, cancellable session.
5. Show success only after receiving a matching acknowledgement. Display the
   returned device MAC and packet source IP. Timeouts must distinguish no
   response, socket/permission error and lost Wi-Fi network.
6. Cancel transmission, close sockets, release callbacks/locks, and remove
   sensitive state when the session ends. Do not log or upload credentials.

The firmware example defaults to v2 with no AES key. For encrypted v2, configure
the same 16-byte key on both sides. Its demo sdkconfig key is not secure storage.
Version 2 uses a fresh transmitted IV; version 1 uses the legacy zero IV.
CBC, CRC and acknowledgement fields do not authenticate the sender/device.

## Wire and acknowledgement details

For v2, header/bit-plane lengths and CRC rules are in ESPTOUCH-V2.txt. The
port mark is 0 through 3. Listen for exactly seven bytes on
`18266 + 10000 * port_mark`: mark followed by six MAC bytes. Check length,
mark and plausible MAC; deduplicate repeated replies. This is confirmation
of a compatible response, not cryptographic device identity.

These are ordinary UDP payload lengths. Do not add the lab harness's raw-frame
padding or wire overhead to Android datagrams. The receiver estimates constant
overhead while capturing frames. Pacing, AP forwarding, multicast destination
choice, power saving and retransmission behavior require real-phone testing;
the length codec alone does not specify a validated Android transport recipe.
Start with a documented experimental pacing value (e.g. 20 ms) and measure it.
Do not claim the two-board raw transmitter validates phone/AP forwarding.

The first release should use the IPv4 path. Keep reserved-data entry optional.
Do not silently trim SSIDs/passwords or count characters instead of bytes.
Require a nonempty SSID at the application boundary. Generate group padding
with the specified width/checksums, independently of AES PKCS#7 padding.

Later options, each requiring its own end-to-end tests:

| Protocol | Acknowledgement used by the example | Scope |
| --- | --- | --- |
| ESP-Touch v1 | 11 bytes to sender IPv4, UDP 18266 | See CAPTURE-V1.txt and sc_touch_make_ack |
| ESP-Touch v2 | 7 bytes broadcast, port selected by mark | Plaintext and AES security1/2 receiver |
| AirKiss | 1-byte token broadcast, UDP 10000 | Full-message cleartext only; no complete vendor discovery/ACK lifecycle |

## Current Android integration requirements

Recheck official documentation against the app's actual target SDK when work
starts. As checked on 2026-10-08, Android 17 requires `ACCESS_LOCAL_NETWORK`
at runtime for broad LAN access when targeting SDK 37 or newer; UDP sending
and reception are covered. Handle refusal/revocation as a normal user-visible
state. Android 16 has an opt-in restriction test mode.
[Local network permission](https://developer.android.com/privacy-and-security/local-network-permission).

Wi-Fi permission requirements depend on the exact APIs used. Do not assume
`NEARBY_WIFI_DEVICES` grants every scan, SSID or BSSID access: audit the
location-sensitive APIs separately. Provide a manual entry/error path when
network metadata is unavailable, rather than accepting placeholder addresses.
[Wi-Fi permissions](https://developer.android.com/develop/connectivity/wifi/wifi-permissions).

Bind provisioning sockets to the chosen Wi-Fi `Network` where appropriate;
Android exposes `Network.bindSocket(DatagramSocket)` for that purpose. Handle
network loss and unregister callbacks after the session.
[Network API](https://developer.android.com/reference/android/net/Network).

Keep the initial UI foreground-only and cancellable. Do not promise background
or screen-off operation without implementing and testing the relevant platform
lifecycle. No account, analytics, cloud inference or remote service is needed.

## Acceptance evidence

- Encoder unit tests compare exact lengths against hand-reviewed protocol
  cases, including non-ASCII byte lengths, empty reserved data, padding
  boundaries, group CRCs, both AES modes and invalid inputs.
- Decode generated lengths through the portable C receiver, checking exact
  credentials and reserved bytes. This is a compatibility check; supplement
  it with independent vectors so encoder/decoder agreement is not circular proof.
- On a real current Android phone and S3: plaintext and encrypted v2 recover
  exact credentials, associate, obtain DHCP and return the expected reply.
- Repeat after denying/regranting permissions, switching Wi-Fi, enabling
  mobile data, canceling, sending a wrong key, and supplying a wrong password.
- Record phone/Android/target SDK, AP band/channel/security, firmware version,
  attempts and failures without recording private credentials.

Do not mark modern Android compatibility complete from an emulator, successful
UDP send calls, host fixtures or the earlier two-board lab results alone.
