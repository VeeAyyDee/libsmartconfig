# Validation

## Repository integration checks for 0.3.0, 2026-10-09

Verified all 115 entries in the supplied source manifest before merging.
MinGW GCC at `-O2` with strict warnings-as-errors passed the portable, scan
recovery, IDF mock and real-worker suites, including all 48 hopper/guide cases.
The hopper test initially failed because its wall-clock sleep did not guarantee
worker progress on Windows. It now waits for completed worker iterations while
preserving virtual 10 ms guide spacing, 50 ms dwell and the exact four-packet
acquisition assertion. No receiver runtime changes were needed.

Real host AES checks passed (36 fixtures, 12 emitted sequences, 262 decrypt calls).
All 2,100 saved Android sender vectors decoded with the updated C receivers, and
the link-audit tests passed. Existing Android app/test AP files, SDK build hook,
signing ignores, CI audit check and Windows test fixes were preserved.

No new IDF build, flashing or phone/RF testing was performed in this integration.
Sanitizers remain unavailable in the installed Windows GCC toolchain; upstream
and CI sanitizer results are separate from these local checks. The supplied
[parity hardware report](PARITY-VALIDATION.txt) retains its original scope.

Both manifests describe the merged tree using LF-normalized UTF-8 text and exact
binary bytes. `SOURCE-MANIFEST.json` excludes both manifests to avoid circular
hashes; `MANIFEST.sha256` includes that JSON manifest and excludes only itself.

## Version 0.3.0 parity validation (implementation-side)

Host tests use injected scan records and synthetic packets, with no ambient
network activity. They cover owned asynchronous scanning, event-loop fences,
weak/empty scan filtering, scan cancellation, registration rollback, connected
STA disconnect, APSTA hopping, exact50/100ms dwell boundaries, retune failure
rearming, and real-discovery event ordering. Recovery tests cover omitted SSIDs for v1, plaintext AirKiss and v2, including
v2 encrypted password/reserved data. Negative tests cover hidden/wrong length,
SSID CRC, BSSID, protected-frame mismatch and conflicting duplicate scan records.
AirKiss tests cover every password length0..64, short/full final blocks, bad CRC
and a late hint after contradictory SSID blocks. Untouched150ms dwell is tested
before explicit option setters. Existing protocol/encryption regressions pass. Target and hardware results for this revision must
be supplied separately by the supervisor; older evidence below is historical.
The supplied [supervisor report](PARITY-VALIDATION.txt) records controlled v1
hardware checks and their limits; no new AirKiss/v2 RF or phone claim follows.

## Version 0.3.0 guide-acquisition regression

The follow-up guide-acquisition regression uses real worker tasks with a virtual
sender clock,50ms dwell,10ms guide spacing and reception only on the sender's
channel. The preserved pre-fix probe failed12/12 fast cases across2–4 channels
and four phases. The matching post-fix probe acquired on packet4 in24/24 normal
and fast cases. Both protocols additionally enumerate all256 four-symbol windows
(including24 permutations and duplicate negatives). The integrated later-channel
suite covers48 v1/AirKiss single/combined cases and scan-sourcedSSID with an empty
password. These are controlled host timing results; hardware outcomes belong to
the supervisor. See parity03/04 for original-only evidence and remaining weak-RSSI
survey differences.

## Repository integration checks for 0.2.0, 2026-10-09

The supplied 0.2.0 snapshot's 97 manifest entries were verified before merging.
The Android app, test AP, signing ignores and previous phone evidence were retained.
Windows MinGW GCC passed the portable and IDF mock suites at `-O2` with strict
warnings treated as errors, including the new pthread compatibility-worker suite.
Real host AES checks passed (36 fixtures, 12 emitted sequences, 262 decrypt calls),
as did the synthetic link-audit suite. The audit fixture now also runs on Windows;
an existing test buffer was zero-initialized to satisfy GCC's optimized analysis.

ASan/UBSan were not rerun locally because the installed Windows toolchain lacks
their runtime libraries. The new IDF 6 build and hardware tests were not repeated
here; the supplied upstream results below retain their original scope. CI runs
the strict host, IDF mock, link-audit and sanitizer suites on Linux.

`MANIFEST.sha256` covers the merged publishable files, excluding itself. Text
hashes use LF line endings, matching Git's normalized text; binary hashes use the
exact file bytes. The imported snapshot's manifest hash is retained in provenance.

## Version 0.2.0 standard API integration

Host checks pass with strict warnings and ASan/UBSan (LeakSanitizer disabled in
this traced environment). They include a real pthread worker, start/stop/restart,
startup rollback, event retries/order, stop from an event handler, single and
combined reception, timeout reset, APSTA channel preservation, copied v2 keys
and reserved-data padding. Mock radio and crypto calls alone are not hardware
or cryptographic validation. Existing independent portable AES tests also pass.
ThreadSanitizer could not run in this environment (unexpected memory mapping).

The unmodified ESP-IDF 6.0.3 SmartConfig example application compiled for S3
using the replacement build hook. Link-map/symbol checks confirm the standard
SDK wrapper and SC_EVENT, our internal receiver, and no linked members from the
proprietary SmartConfig archive. Application code was not rewritten.

Fresh two-board standard-API ESPTouch v1 A/B passed on 2026-10-08. The same
application flow ran with the stock receiver and our receiver. Both delivered
matching synthetic credential bytes, associated with the lab AP, obtained DHCP,
completed UDP echo, and completed the SDK ACK sequence. FOUND_CHANNEL,
GOT_SSID_PSWD and SEND_ACK_DONE order was checked. Both saved PHY calibration
and both original full-flash snapshots were restored and verified afterward.
Boards remained in download mode for the following ESP-NOW test. See
[the receipt](../validation/standard-api-ota.json).

That test covers the new standard workflow with v1, not every protocol/mode.
Historical 0.1.x results below cover the portable/optional interfaces and do not
by themselves validate every path through the new compatibility worker. In that historical version, fast mode, scan-assisted recovery and discovery were unsupported;
see README.md for the supported workflow and limits.

## Historical portable/optional API validation

### Android sender

The [Android test app](../android/README.md) has a separate
[validation record](../android/VALIDATION.md). All six sender modes passed with
a POCO X3 NFC running Android 11 and two ESP32-S3 boards (WPA2 AP plus receiver)
using ESP-IDF 5.5.2 and the optional polling example. This phone evidence does
not establish coverage of the new standard API worker or every Android/router.

### Original receiver and example release evidence

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
- Historical tests below used the optional `sc_*` API. Version 0.2.0 adds the
  standard SDK workflow; its separate checks are listed above. This does not
  establish every private vendor symbol or behavior. Wi-Fi/PHY remain SDK code.
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
