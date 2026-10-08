# SmartConfig Test for Android

A small foreground sender for this repository's ESP32-S3 receivers. Original
Java code is 0BSD. No account, analytics, cloud service, or third-party sender
implementation is used. The app includes:

| Mode | Firmware selection | Encryption |
| --- | --- | --- |
| ESP-Touch v1 | `CONFIG_SC_EXAMPLE_PROTOCOL_V1` | Plaintext |
| ESP-Touch v2 | `CONFIG_SC_EXAMPLE_PROTOCOL_V2` | Plaintext, AES security 1 or 2 |
| AirKiss | `CONFIG_SC_EXAMPLE_PROTOCOL_AIRKISS` | Plaintext or AES password mode |

The optional AES field accepts **exactly 32 hex digits decoded into 16 bytes**.
Use that same value in `CONFIG_SC_EXAMPLE_V2_KEY` or
`CONFIG_SC_EXAMPLE_AIRKISS_KEY`. V2 security 2 transmits a fresh IV; security 1
uses a zero IV. AirKiss uses the raw key as its IV and encrypts only the password.
An empty AirKiss password produces no ciphertext. V2 uses security 0 when both
password and reserved data are empty, even if an AES mode was selected.

## Build and install

Open this `android` directory in Android Studio, or use JDK 17+ and the Android
SDK (platform 37.0 and build tools 36.0.0):

```powershell
$env:JAVA_HOME = Join-Path $env:ProgramFiles 'Android/Android Studio/jbr'
$env:ANDROID_HOME = "$env:LOCALAPPDATA\Android\Sdk"
.\gradlew.bat :app:assembleDebug :app:lintDebug :codec:check
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Use `./gradlew` on Linux/macOS (or `sh gradlew` if the executable bit is absent).
The Gradle distribution is pinned and checksum verified. Android Gradle Plugin
9.1.1 and Gradle 9.3.1 are pinned. Minimum Android is 8.0/API 26; compile and target
SDK are 37. Successful compilation is not a claim of testing every Android version.

## Signed release APK

Keep the release keystore outside this repository, with a secure backup. On
Windows, build with a local password prompt from the repository root:

```powershell
.\android\tools\build_release.ps1
```

The default keystore is `%USERPROFILE%\.android\libsmartconfig-release.jks`,
alias `libsmartconfig`. Enter the key password only if it differs from the store
password. For a local password dialog use `powershell.exe -NoProfile -STA -File
.\android\tools\build_release.ps1 -UseDialog`. Passwords are passed to Gradle
through process environment variables, cleared afterward, and never written to
the repository. The helper disables the persistent daemon and configuration cache.

It builds and checks the release, verifies the signature and non-debuggable flag,
then produces `android/dist/SmartConfig-Test-0.1.0.apk`, `SHA256SUMS.txt`, and the
public signing certificate fingerprint. Attach the APK and checksums to a GitHub
Release such as `android-v0.1.0`. Do not attach the keystore or passwords. These
outputs and common signing-key file names are Git-ignored.

For other environments, provide `SMARTCONFIG_KEYSTORE`, `SMARTCONFIG_KEY_ALIAS`,
`SMARTCONFIG_STORE_PASSWORD`, and optionally `SMARTCONFIG_KEY_PASSWORD`, then run
`./gradlew --no-daemon --no-configuration-cache :app:assembleRelease :app:lintRelease :codec:check`.
Missing passwords fail the release build rather than silently creating an unsigned
APK. Debug builds remain independent of release credentials.

The permanent release certificate differs from the debug certificate used during
phone testing. Android will require uninstalling that debug installation before
the first release installation. Future releases must retain the same signing key
and increment `versionCode` (and normally `versionName`) in `app/build.gradle`.

## Use

1. Connect the phone to the intended **2.4 GHz** access point and boot the
   firmware with the matching protocol/key. The example handles one session per
   boot, so reboot it between attempts.
2. Open the app and allow permissions. Select the Wi-Fi network, check SSID,
   BSSID and frequency, and enter the exact password. SSID/password whitespace
   is preserved. Limits apply to UTF-8 bytes: SSID 1–32, password/reserved 0–64.
3. Select the protocol and encryption mode, and confirm that the network and
   radio have been checked. Reserved text is available only for v2.
4. Start provisioning and keep the app visible. It listens before sending,
   binds both UDP sockets to the selected non-VPN Wi-Fi `Network`, and stops
   after a matching reply, cancellation, Wi-Fi/IP loss, error, or 120 seconds.
5. ESP-Touch replies display the device MAC and packet source IPv4. AirKiss's
   one-byte reply contains only a token, so its device MAC is **unavailable**.

The transport is experimental: UDP destination `239.255.0.1:7001`, 20 ms packet
spacing, repeating complete guide/header/body cycles. Lengths are UDP payload
lengths without raw-frame padding. Real AP forwarding, aggregation and power
saving behavior still need to be measured on the intended phone/AP.

### Permissions and lifetime

Android 17/API 37 local networking requires runtime `ACCESS_LOCAL_NETWORK`.
The app requests it on API 37+. It also declares/requests nearby Wi-Fi permission
on API 33+ (including Android 16's opt-in LAN restriction testing).
Precise location permission and enabled system Location services may be needed
for SSID/BSSID metadata; nearby permission is not a substitute. Metadata comes
from the selected network's callback `WifiInfo`, using the
location-information flag on API 31+. API 26–30 falls back to connected Wi-Fi info.
No scanning APIs are used. Missing metadata has a manual verification path;
placeholder BSSIDs and known 5/6 GHz connections are rejected.

Leaving the foreground, including opening Settings, cancels an active session.
Sockets close on cancellation, the multicast lock is released, and network
callbacks are removed when the activity stops. The screen stays on only during
sending. Password, AES and reserved fields are cleared after starting/ending a
session and when leaving the app. Mutable credential buffers and encoded lengths
are cleared; Java/Android temporary copies cannot guarantee secure memory erasure.
No credentials are logged, saved, backed up, or uploaded. Screenshots are blocked
by the production activity to avoid exposing fields.

### Confirmation limits

V2 checks exact seven-byte length, port mark, and a plausible unicast MAC. V1
checks exact 11-byte length, expected total, MAC, and returned IPv4 against the
packet source. AirKiss checks exact one-byte length and token. The first valid
reply ends the session, so repeats cannot create duplicate results. None of these
formats authenticates device identity. Wrong keys/passwords commonly time out;
the app does not claim it can identify which was wrong from silence.

AirKiss is full-message provisioning with the example's basic token reply, not
the complete vendor discovery/control lifecycle. Passwords beyond 32 bytes and
binary codec cases are this receiver's extension policy. The app takes UTF-8
text and rejects NUL in Wi-Fi SSID/password as required by the firmware boundary.

## Host validation

`codec` has no Android dependencies. `:codec:check` runs a self-contained Java
assertion suite with fixed framing cases, CRC-8/MAXIM's standard check value,
UTF-8 limits, malformed inputs, both v2 AES modes, fresh IVs and reply negatives.
It writes 2,100 synthetic compatibility vectors to `codec/build/vectors.tsv`.

From the repository root, with GCC and Python `cryptography` installed:

```powershell
gcc -shared -O2 -Wall -Wextra -Werror -I include src/sc_touch.c src/sc_touch2.c src/sc_airkiss.c -o android/codec/build/receivers.dll
python android/tools/receiver_compat.py android/codec/build/receivers.dll android/codec/build/vectors.tsv
```

On Linux add `-fPIC` and use `receivers.so`. The compatibility test checks exact
SSID/password/reserved bytes against the real C decoders, using Python's crypto
library for AES decryption. This complements fixed vectors; encoder/receiver
agreement alone is not independent proof of vendor conformance.

See [validation](VALIDATION.md) for measured results and pending phone tests.

## Two-board phone lab

The isolated AP source is in `examples/test-ap`. Its **public test credentials**
are SSID `SmartConfig-Lab`, password `LabPass123`, WPA2-PSK, channel 6. It supplies
DHCP on `192.168.4.0/24` and has no internet uplink. Tell Android to stay connected
if it warns about no internet. Do not use these credentials for a real network.

The provisioning example and test AP were built with the user's installed
ESP-IDF 5.5.2. The component's published manifest still requires IDF 6.x; for
this explicit 5.5 compatibility test only, set `IDF_COMPONENT_MANAGER=0` before
building `examples/provision`. This does not change published SDK support.

A separate instrumentation APK drives the real UI with these public lab
credentials. It is not included in the user APK:

```powershell
.\gradlew.bat :app:assembleDebugAndroidTest
adb install -r app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk
adb shell am instrument -w -e protocol 0 -e mode 0 -e bssid 80:65:99:df:56:81 dev.libsmartconfig.testapp.test/dev.libsmartconfig.testapp.LabInstrumentation
```

`protocol`: 0=v2, 1=v1, 2=AirKiss. `mode`: 0=plaintext, 1=v2 security 2 or
AirKiss AES, 2=v2 security 1. The public test AES key is
`000102030405060708090a0b0c0d0e0f`. Firmware must use the same mode/key.
`scenario` can be `success`, `cancel`, `background`, `wifi-loss`, `wrong-password`, or
`wrong-key`. Grant app permissions and join the test AP before running it.
Reset the receiver before every case. Never pass real credentials through
command-line test arguments. `tools/serial_capture.py` records bounded logs.

`tools/run_phone_case.py` automates receiver reset, concurrent serial capture,
the phone test, and reopening the app afterward. `--capture` captures the form
only after verifying secret fields are empty, then removes the temporary phone
cache files. `tools/build_receiver.ps1` builds mode-specific lab firmware using
explicit IDF and Python-environment paths. These scripts use public lab values
only; they do not read saved phone credentials.

Official API references checked during implementation:
[LAN permission](https://developer.android.com/privacy-and-security/local-network-permission),
[Wi-Fi permissions](https://developer.android.com/develop/connectivity/wifi/wifi-permissions),
[Network socket binding](https://developer.android.com/reference/android/net/Network),
[AGP/API compatibility](https://developer.android.com/build/releases/agp-9-1-0-release-notes).

Gradle wrapper files retain Gradle's Apache-2.0 license; Android build tooling
and installed SDKs retain their own terms. See repository `LICENSE` for new code.
