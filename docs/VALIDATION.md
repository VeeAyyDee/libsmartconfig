# Validation and release scope

The runtime source implements ESP-Touch v1, full-message plaintext/keyed AirKiss and
ESP-Touch v2 plaintext/security1/security2. Validation has two separate scopes:
the original receiver code tested in lab harnesses, and the newly packaged
application example. The example has not itself been flashed/tested on hardware.

## Receiver evidence

| Protocol | Independent host checks | Controlled hardware comparison |
| --- | --- | --- |
| ESP-Touch v1 | 67,081 core and 2,341 capture assertions | One credential case, original and new receiver |
| AirKiss plaintext | 71,386 assertions; 2,147 public-sender ASCII vectors; 256 binary-policy cases | Two message sizes, original and new receiver |
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

## Encrypted AirKiss added in 0.1.1

Independent host validation passed 1,056 native ARM sender sequences, including
reset/config-copy checks, 192 longer-password policy cases, 140 invalid-padding
and length cases, and 8,448 synthetic capture/header combinations using real AES.
The original sender artifact supports passwords through 32 bytes; longer and
binary-byte cases test our extension policy. The public host AES test reproduces
36 crypto fixtures and 12 selected native sequences; the full private oracle
corpus is not bundled.

A fresh two-S3 radio test passed five runs: password/key lengths 8/16, 16/8
(short key zero-padded), 32/16 (all-zero key), an empty-password/open-network
case with keyed mode selected, and a known wrong-key negative. Positive runs
recovered exact credentials/token, associated, obtained DHCP, exchanged UDP
echo and delivered the matching basic AirKiss ACK. The wrong-key run acquired
the channel but emitted no credentials during its 45-second receive window.
All runs had an eight-second silent pre-send check and zero sender TX errors.

PHY calibration was verified in fresh test NVS; calibrated NVS snapshots remain
private. Both original 16 MiB flash images were restored and fully verified.
See [hardware receipt](../validation/airkiss-encrypted-ota.json) and
[supervisor feedback](../spec/SUPERVISOR-FEEDBACK-09.txt).

This is controlled raw, unprotected non-QoS From-DS radio traffic carrying an
AES-encrypted provisioning password. It is not a phone test, not protected
AP-forwarded traffic validation, and not an original encrypted receiver A/B.
The release example was compiled, while hardware used a separate lab harness.
The underlying vendor Wi-Fi/PHY remains; original SmartConfig blob members
were absent from the test images. CBC has no authentication guarantee.

## Limits

- The lab sender emitted controlled raw, unencrypted non-QoS From-DS frames.
  V2's provisioning payload AES was real, but radio-envelope encryption was
  not simulated. These tests do not establish encrypted AP-forwarded or actual
  Android/iOS phone interoperability.
- The earlier plaintext AirKiss validation used a community public sender, not official Tencent
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
