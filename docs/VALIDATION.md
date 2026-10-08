# Validation and release scope

The runtime source implements ESP-Touch v1, full-message cleartext AirKiss and
ESP-Touch v2 plaintext/security1/security2. Validation has two separate scopes:
the original receiver code tested in lab harnesses, and the newly packaged
application example. The example has not itself been flashed/tested on hardware.

## Receiver evidence

| Protocol | Independent host checks | Controlled hardware comparison |
| --- | --- | --- |
| ESP-Touch v1 | 67,081 core and 2,341 capture assertions | One credential case, original and new receiver |
| AirKiss | 71,386 assertions; 2,147 public-sender ASCII vectors; 256 binary-policy cases | Two message sizes, original and new receiver |
| ESP-Touch v2 | 14,935 core, 6,697 capture and 1,508 stream-entry assertions; 447 public-sender vectors | Plaintext and both AES modes, original and new receiver |

The completed radio comparisons recovered exact credentials, associated with
the lab WPA2 AP, obtained DHCP, exchanged a UDP echo and delivered the checked
acknowledgement. V2 also recovered exact reserved bytes using actual on-device
PSA AES decryption. Tests started from empty test NVS and verified saved PHY
calibration. Both boards' full original flash images were restored and verified.

The v2 first trial exposed a guide-overlap acquisition bug. A bounded sliding
four-length matcher fixed it; a separate regression then passed all 377 entry
points of three repeating sender streams. The failed attempt remains in the
maintainer's local evidence. One million core plus one million malformed
capture probes passed sanitizers for each of the AirKiss and v2 extensions;
LeakSanitizer was disabled in the traced environment.

New v2 security2 reported capture channel 10, then associated on primary 6 in
40U mode. Exact credentials and network exchange passed, but the channel
observation's cause is unproven. Do not force association to the captured
channel. Plaintext/security1 reported channel 6 in that run.

## Limits

- The lab sender emitted controlled raw, unencrypted non-QoS From-DS frames.
  V2's provisioning payload AES was real, but radio-envelope encryption was
  not simulated. These tests do not establish encrypted AP-forwarded or actual
  Android/iOS phone interoperability.
- AirKiss validation used a community public sender, not official Tencent
  conformance. That sender's code is not distributed here. Binary byte cases
  beyond its ASCII behavior test our stated policy, not phone compatibility.
- This is an original API, not full `libsmartconfig.a` ABI compatibility.
  Underlying vendor Wi-Fi/PHY code remains in hardware builds.
- No authentication guarantee follows from CRC, tokens or CBC padding.
- No production reliability or broad phone/AP compatibility claim is made.

## Reproduction available here

Host unit tests, owned IDF/PSA mocks and sanitizers are available through the
Makefile. The standalone IDF example and archive/symbol audit are included.
The public release includes concise build receipts under `validation/` after
package validation. These establish compilation/linkage, not a new radio trial.

The specifications, dated supervisor feedback and provenance ledger preserve
the reported lab results. Private firmware backups, raw logs, sender source
and the supervisor's broader reverse-engineering workspace are not included;
this package alone cannot reproduce every historical A/B measurement.
