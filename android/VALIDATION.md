# Android sender validation

Recorded 2026-10-08. Repository base: `ad3aa67`.

Published build logs replace the local checkout's absolute path with `<repo>`.
This redaction changes only workstation paths, not test output or results.

| Check | Result |
| --- | --- |
| Android APK, target/compile API 37 | Built with AGP 9.1.1, Gradle 9.3.1, JDK 21 |
| Android lint | Passed; English-only text/compatibility attribute warnings remain |
| JVM encoder and acknowledgement suite | 330 assertions passed |
| Real portable C receiver decoding | 2,100 passed: v1 100, v2 1,800, AirKiss 200 |
| AES compatibility | JVM encryption, Python cryptography decryption callback into C; v2 security 1/2 and AirKiss |
| ESP-IDF 5.5.2 receiver example | Built; flashed and booted on COM432 ESP32-S3 |
| ESP-IDF 5.5.2 isolated AP | Built; flashed and booted on COM6 ESP32-S3 |
| Actual phone provisioning | All six modes passed on POCO X3 NFC (M2007J20CG), Android 11/API 30 |
| Manual cancel / leaving foreground | Passed; sockets close and sensitive fields clear |
| Wrong AirKiss AES key | Passed: no credential completion on receiver; phone timed out without success |
| Wrong Wi-Fi password with correct AirKiss key | Passed: association failed after three attempts; no phone success |
| Wi-Fi disabled during transmission | Passed after fixing socket/network callback ordering; specific lost-network result |
| Location denied / manual SSID and BSSID | Plaintext v2 passed with coarse and fine location revoked; permissions restored afterward |
| Signed release APK | Built with the user's permanent release key; signature verified and non-debuggable flag checked |
| Release build checks | Release lint and 330 JVM assertions passed; missing-password release build correctly refused |

The signed release and checksum are generated under `android/dist` (Git-ignored).
The public certificate/APK fingerprints are recorded in
`validation/release-receipt.json`. Release signing uses process environment
variables, a single-use Gradle daemon, and no configuration cache. Passwords are
entered through a local prompt and cleared by the helper. The release APK has
not replaced the debug installation used for the phone tests above.

Board boot logs are in this directory's `validation` subdirectory. AP MAC is
`80:65:99:df:56:81`, receiver station MAC is `64:e8:33:43:ff:5c`. AP is WPA2,
2.4 GHz channel 6, IPv4 `192.168.4.1`. Both chips report revision v0.2, 8 MiB PSRAM.
The user authorized replacing firmware/data. IDF 6 download was canceled and
removed; no IDF 6 build or installation was used. ADB 35 initially reported
unauthorized without a prompt; after revoking phone authorizations and using
Google platform tools 37.0.1, the phone authorized successfully.

## Real phone evidence

The phone sent ordinary paced UDP datagrams over the WPA2 AP connection. Receiver
logs show capture lock, successful association, DHCP (`192.168.4.3`) and ACK
transmission. The real app UI reported a matching MAC/IP for ESP-Touch and token/IP
for AirKiss. These are phone/AP-forwarded tests, not the earlier raw-frame harness.

| Mode | Phone result | Receiver evidence |
| --- | --- | --- |
| ESP-Touch v2 plaintext | Matching MAC/IP reply | `validation/v2-plain-phone.txt` (receiver log) |
| ESP-Touch v2 plaintext, final APK/manual metadata | Pass | `validation/v2-location-denied-receiver.txt` |
| ESP-Touch v2 security 2 | Pass | `validation/v2-security2-receiver.txt` |
| ESP-Touch v2 security 1 | Pass | `validation/v2-security1-receiver.txt` |
| ESP-Touch v1 | Pass | `validation/v1-receiver.txt` |
| AirKiss plaintext | Pass on retry | `validation/airkiss-plain-retry-receiver.txt` |
| AirKiss AES | Pass | `validation/airkiss-aes-receiver.txt` |

Paired `*-phone.txt` and JSON receipts record the instrumentation results. The
initial plaintext-v2 run preceded the combined logger; its phone output was
observed directly and the receiver log is retained. Successful cases used the
public lab SSID/password and AES key documented in README. Non-ASCII/max-length
and exact reserved-byte comparisons are host coverage, not these RF cases.

Two test-runner problems are retained in the evidence: the first plaintext
AirKiss run canceled on backgrounding because a duplicate activity was launched;
the script now foregrounds only when necessary. An initial cancellation/layout
capture failed because the test package had no external-files directory. After
using the app cache and immediately removing captured files, cancellation and
layout capture passed. Neither failed attempt is counted as a protocol pass.
The first Wi-Fi-loss test exposed a real classification race: an `IOException`
could arrive before `onLost`, producing a generic socket-error message. The
original test asserted only that the message contained "Wi-Fi", which was too
weak. The sender now checks Wi-Fi/network state and briefly allows the callback
to arrive, and the test requires the specific lost-network/IP-change outcome.
The initial receipt is retained as evidence of that failure, not a verified pass.

Final lab state: COM6 remains the isolated WPA2 access point; COM432 is flashed
back to plaintext v2, and the phone has the final APK installed with location
permissions restored. The app opens normally without the instrumentation runner.
Reboot COM432 before another manual attempt because the receiver's session is
bounded to 120 seconds and runs once per boot.

Early v2 timings include MIUI delaying the test activity launch, so they are not
reliable protocol performance measurements.

The top and bottom UI screenshots in `validation` were visually inspected on the
actual 1080×2400 phone. Published screenshots are cropped to 1080×2300 by removing
the top 100 pixels containing the phone status bar; retained pixels are unchanged.
Only the separate test APK temporarily removes screenshot
protection, after checking secret fields are empty. The app retains `FLAG_SECURE`.

Remaining coverage includes modern Android 17 LAN permission refusal/revocation,
other phones and routers, repeated reliability measurements, roaming/band changes,
and mobile-data coexistence (this phone reported no mobile data service). Passing
on Android 11 does not establish modern Android compatibility simply because
the APK targets SDK 37.
