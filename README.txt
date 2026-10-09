libsmartconfig 0.3.0: original receiver for the ESP-IDF SmartConfig workflow

The default application interface is esp_smartconfig.h and SC_EVENT, with an
autonomous receiver task. The SDK's open-source start/stop wrapper and ACK
machinery remain in place; our library supplies their internal receiver and
remaining public options. The built IDF archive is libsmartconfig.a. Existing
sc_* portable and explicit keyed AirKiss APIs remain optional extensions.
This is bounded public-workflow compatibility, not a claim to reproduce all
private ABI, receiver heuristics or phone behavior. See the limits below.

Behavioral inputs are spec/DROP-IN-01.txt, ESPTOUCH-V1.txt, CAPTURE-V1.txt,
AIRKISS-V1.txt, AIRKISS-ENCRYPTED-01.txt and ESPTOUCH-V2.txt. New code uses
0BSD, selected by the user on 2026-10-08; see LICENSE. The supplied public SDK
header and its license under spec/public-sdk retain Apache-2.0. SDK and other
third-party software are not relicensed.

Build and test
--------------
  make                 Build libsc_touch.a and libsc_touch.so.
  make test            Build/run strict warnings-as-errors unit tests.
  make sanitize        Run tests with AddressSanitizer and UBSan.
  make test-idf        Run IDF lifecycle/PSA mocks and real-worker compatibility tests.
  make test-crypto     Run real host AES tests (Python cryptography required).
  make sanitize-idf    Run those mocks with ASAN and UBSan.
  make test-audit      Verify approved/archive contamination audit behavior.
  make clean           Remove local build products.

Under a traced environment where LeakSanitizer cannot operate, use
  make sanitize ASAN_OPTIONS=detect_leaks=0
This still runs AddressSanitizer and UBSan, but does not check for leaks.

CC, AR, CPPFLAGS, CFLAGS and LDFLAGS may be overridden. The source uses only
standard C11 library facilities; Makefile shared-library/PIC/sanitizer flags
target toolchains supporting the usual Unix compiler options.

ESP-IDF component and standard example
--------------------------------------
The root component registers six portable and five IDF source files, uses
public include/ and idf/ directories, and emits libsmartconfig.a. Manifest
version 0.3.0 targets ESP32-S3 and ESP-IDF >=6.0,<7.0. Required SDK components
are esp_wifi, esp_event, esp_timer and mbedtls. The host Makefile remains
independent of IDF and preserves its existing libsc_touch.a/.so names.

Use examples/standard as the default application. It uses only the standard
esp_smartconfig.h provisioning API; there is no caller polling or sc_* API
migration. From that directory in an activated IDF environment:
  idf.py set-target esp32s3
  idf.py menuconfig
  idf.py build
The project produces libsmartconfig_example.elf. Select protocol0..3 with
CONFIG_LIBSMARTCONFIG_EXAMPLE_TYPE (default2 combined) and an optional v2 key
with CONFIG_LIBSMARTCONFIG_EXAMPLE_V2_KEY (32 hex digits, decoded to16 bytes).

The application's top-level CMake must call libsmartconfig_replace_sdk after
project(), as shown in examples/standard/CMakeLists.txt. The hook sets the
existing SDK imported esp_wifi_smartconfig archive location to this built
component archive. Main must explicitly require this component. It does not
link two competing receiver archives or replace SDK public wrapper/ACK code.
The hook rejects an unsupported SDK target layout; SDK-major6 support does
not imply every minor version has been tested. No absolute local paths are
embedded. The package can be renamed because the example derives its folder
name. See examples/standard/README.txt for link audit and lifecycle details.

Public workflow and limits
---------------------------
SC_EVENT_FOUND_CHANNEL precedes exactly one SC_EVENT_GOT_SSID_PSWD per
successful session; failed event posts retry without duplicate successful
credential posts. No scan is implemented and SCAN_DONE is not fabricated.
Application handlers configure/connect Wi-Fi and stop after SDK ACK_DONE,
as in the ordinary SDK workflow. The receiver never auto-connects. Stop
must be called after completion before another receiver starts. All public
lifecycle/option calls are serialized internally; a simultaneous API call
returns ESP_ERR_INVALID_STATE rather than blocking on another caller.

A fixed128-frame ring copies only bounded headers in the Wi-Fi callback.
A4096-byte worker stack drains bounded work every10ms. Stop disables ring
acceptance, asks the worker to exit, waits up to2seconds, then removes the
callback and restores filter/channel state before freeing decoders. A
failed stop/rollback retains state for retry; start is rejected until cleanup
finishes. SDK event posts have zero wait, so event-handler stop can join the
worker without a delivery deadlock. Stop from the worker itself signals
shutdown and returns INVALID_STATE rather than self-joining; retry from an
application task cleans retained resources. SDK-owned queued event copies
may still be delivered after stop; keep application handler state valid.

Default type2 feeds ESPTouch and plaintext AirKiss through one radio owner.
Either candidate lock retains the channel; the first complete valid message
wins. Types0/1/3 select ESPTouch/AirKiss/ESPTouchV2. Start always requests
STA disconnect (errors are logged but tolerated), waits50ms, and discovers
APs with public asynchronous scans. Applications must exclusively reserve
scanning/promiscuous reception and suspend automatic reconnect while active.
APSTA uses the same discovery/hopping path; uninterrupted softAP service is
not guaranteed. Only successfully acquired scans are cancelled on stop.

Discovery retains up to64 distinct BSSIDs with RSSI>-85dBm and country-valid
2.4GHz channels. At least two successful passes precede capture; empty scans
continue discovery. SCAN_DONE follows capture enablement, before FOUND_CHANNEL
and GOT_SSID_PSWD. A generation-tagged event-loop fence drains queued old scan
completions before a new scan starts. Stop quiesces the worker and unregisters
handlers before freeing receiver state. The public SDK cannot arbitrate scans
against unrelated components: application-level exclusive ownership is required.

Untouched default dwell is150ms; fast_mode(false) selects100ms and true selects
50ms, persistent across stop/start. Timings are scheduling requests plus worker
and driver latency. Retune failures keep the actual channel and rearm the dwell;
errors are printed when enable_log is true. Options must be set while inactive,
a deliberate restriction relative to the original's active fast-mode setter.
esp_esptouch_set_timeout accepts15..255seconds and adds45seconds. Expiry clears
decoders and AP cache and starts discovery again. Copied v2 keys survive this
reset, an intentional robustness improvement over the observed original restart.

A copied sc_scan_ap hint bridges discovery and the byte/capture decoders.
Capture accepts hints only for the locked six-byte BSSID, channel and matching
protected/unprotected frame flag. The standard path rejects unknown cipher
metadata and conflicting duplicate BSSID records; it does not attempt ambiguous
CRC-only, hidden-SSID or cross-band recovery. V1 checks SSID length/CRC and
BSSID CRC, fills missing SSID/BSSID bytes without overwriting received bytes,
then checks whole-message XOR. Plaintext AirKiss validates the SSID metadata,
requires password+token and each actual short/full block CRC. V2 can source
SSID from cache after declared length/BSSID CRC checks while preserving all
password/reserved-data CRC and decryption requirements. Previously received
conflicting SSID data blocks prevent completion. Completed results are immutable.
Keyed AirKiss keeps its earlier full-message behavior. These are conservative
recovery rules, not full equivalence with all proprietary AP matching heuristics.

V2 encryption copies16 raw key bytes before start returns; enabled with NULL
key is INVALID_ARG. These v2 options do not enable AirKiss encryption. Public
AirKiss is plaintext because the standard config has no AirKiss key argument;
explicit keyed AirKiss remains available through the optional APIs. V2
reserved-data getter is available after a complete v2 result until stop,
accepts requests0..64 with a non-NULL buffer, copies available bytes and
zero-fills the requested remainder. It never exposes uninitialized bytes.

The standard credential event uses fixed string arrays, so empty SSIDs and
embedded-NUL credentials are rejected even though portable byte codecs can
represent them. Maximum32-byte SSID/64-byte password fields may fill their
arrays completely; use bounded copies. Standard v2 currently accepts IPv4
only. Capture channel is not guaranteed AP primary; association is not
pinned to it, and stop preserves the channel of a newly connected station.
CRC/CBC/padding do not authenticate provisioning or reliably identify a wrong
key. Wi-Fi/PHY and SDK ACK implementation remain external dependencies.

Owned standard-workflow tests use real pthread workers with public-API Wi-Fi
and event mocks. They cover start/stop/start, every probed Wi-Fi API startup
failure, task-creation failure, cleanup retry, active option rejection, single
and combined capture, event retry/order, asynchronous event-handler stop,
APSTA discovery/hopping, timeout rediscovery, v2 key copying/decrypt
failure and reserved zero-fill. Strict warnings-as-errors and ASAN/UBSan pass;
LeakSanitizer is disabled in the traced environment. Synthetic link-audit
fixtures cover approved paths with spaces, contamination and missing symbols.
These owned tests do not replace SDK-wrapper, real-radio or phone validation.

Optional extension example
---------------------------
examples/provision keeps the previous sc_* polling workflow, including
CONFIG_SC_EXAMPLE_AIRKISS_KEY for encrypted AirKiss. Its package/version now
follow libsmartconfig; its source API remains available. That application
owns replies itself. It is an optional integration path, not the default
standard-API example. Earlier0.1.0/0.1.1 frozen exports are not overwritten.

Optional portable decoder use
------------------------------
Include sc_touch.h, create a context with sc_touch_create(), and feed one
normalized UDP payload length at a time through sc_touch_feed(). A return
value of 1 means sc_touch_get_result() can copy complete verified credentials.
Return 0 means incomplete, -1 means NULL context, and -2 means a conflicting
indexed byte poisoned the session. A result stays frozen until reset.
Call sc_touch_destroy() when done. Allocation failure returns NULL.

SSID and password are counted byte arrays and can contain zero and high-bit
bytes. They are not NUL-terminated strings. The output struct is only copied
on completion. Caller-provided output remains untouched on failure. See the
header for the full API, including a stateless triplet decoder and CRC8.
The triplet decoder modifies outputs only on success. NULL CRC8 input returns
zero even with nonzero length; this fallback does not detect caller errors.

Policy and state
----------------
Each triplet encodes one indexed byte with a per-triplet CRC. A sliding window
accepts arbitrary alignment; normalized lengths outside 40..423 break only
the partial triplet window. In particular guide lengths 515..512 are ignored.
Malformed triplets leave collected bytes intact. All 128 possible indices are
tracked, so even a conflict at an otherwise unused index poisons the context.
Same-value duplicates are harmless. Conflicting indexed bytes poison the
context; source/session separation remains the caller's responsibility.

Completion requires T in 9..105, password length in 0..64, SSID length in
0..32, all T+6 required indices without a scan hint, whole-message XOR, SSID CRC, and BSSID CRC.
BSSID indices may arrive before or interleaved with message indices. Empty
SSID/password arrays are permitted by this byte codec without implying an
access point can be provisioned with them. Integrity failures and invalid
header lengths remain incomplete; changing an already received byte is a
conflict and requires an explicit reset. CRC and XOR are integrity checks,
not cryptographic authentication.

There are no global sessions. Contexts are independent, but the caller must
serialize access to each context and separate sources/provisioning sessions.
Reset clears collected bytes, result, poison status, and framing state.
Destroy clears the context before freeing it. Ordinary portable C memset is
not a guaranteed secure erasure primitive; an optimizing compiler may remove
a clear when the storage will no longer be observed.

Tests
-----
Local tests exhaust all 256 byte values and 128 indices, reject every invalid
16-bit length in each triplet position, and reject checksum corruptions.
Stream tests exercise every permitted password/SSID length pair, shuffled
index delivery, duplicates, guide interruptions, partial framing resets,
missing data, byte preservation, independent contexts, completion freezing,
conflicts at every index, invalid sizes, and each whole-message check.
These fixtures are generated from the permitted specification. They are not
the supervisor's independent public-sender compatibility oracle.

Portable capture adapter
------------------------
sc_capture.h accepts a header prefix (24 bytes for Data or 26 for QoS Data),
total captured wire length including FCS, channel and monotonic uint32_t
milliseconds. It does not dereference payload or retain caller pointers.
It parses bytes explicitly, filters multicast/broadcast DS data frames and
rejects fragments, A-MSDU, unsupported framing, invalid addresses and bounds.
Protected frames are supported for length observation without decryption.

Four bounded candidates keep sliding four-length windows. One quartet of
distinct consecutive lengths (normalized512..515), in any order or cycle phase,
locks immediately with inferred overhead0..256. Candidate keys include BSSID, sender, channel,
DS direction, QoS and Protected. Candidates expire after 1500 ms without a
new eligible-header frame for their key. Replacement chooses an unused slot,
otherwise the least recently updated slot, with lowest-slot tie breaking.
Consecutive duplicate Sequence Control values do not advance or refresh it.
Other keys do not alter a retained candidate's sequence. Same-key mismatches
slide through the four-sample window, allowing a later intact quartet to lock.

The parity03/04 addenda supersede the historical eight-guide policy for v1 and
AirKiss. Four10ms-spaced frames fit the50ms fast dwell; partial one/two/three
samples do not hold a channel. This implementation commits all valid same-source
quartets immediately. It does not reproduce the original weak FromDS/multichannel
RSSI survey preference, and retains the wider portable overhead bounds rather
than the pinned original raw frame-length filters. V2 acquisition is unchanged.

State is 0 searching, 1 locked, or 2 complete; NULL feed/tick/state returns -1.
Once locked only matching key/unique consecutive sequences reach the byte
decoder. Underflowing normalization is ignored. Accepted matching activity
refreshes a 2500 ms idle deadline; the absolute lock deadline is 30000 ms.
Unsigned subtraction handles uint32_t clock wraparound. Tick expires state
without frames. Invalid headers do not change state or refresh deadlines.
Conflicts unlock and clear all session data. A decoded BSSID differing from
the selected BSSID also clears the session without exposing credentials.
Completion freezes until reset. Output getters modify output only on success.

lock.channel records the observed capture channel from packet metadata. It
is not guaranteed to be the AP's primary channel. Do not force association
to this value; let the Wi-Fi association process determine the AP channel.

sc_touch_make_ack builds the documented 11-byte reply from result length,
device MAC and local IPv4 bytes; destination UDP port is 18266. The caller
owns the socket, routing, transmission/retry policy and phone integration.

IDF polling adapter
-------------------
idf/sc_touch_idf.c is excluded from the host Makefile. Build it with the two
core source files and include/ plus idf/ include directories in an ESP-IDF
component depending on esp_wifi, esp_event and esp_timer. Its original start,
poll and stop functions do not replace vendor esp_smartconfig symbols.

The caller must initialize the default event loop, start disconnected STA
Wi-Fi, register SC_TOUCH_EVENT handlers, and serialize calls. Poll every
10 ms. Start rejects an already-running adapter, active promiscuous mode,
non-STA mode and existing association; SDK operation failures propagate.
The adapter owns the promiscuous callback exclusively while active. It does
not initialize/start/stop Wi-Fi, edit NVS, store credentials, or associate.
There are no credential logs and no adapter background worker.

The Wi-Fi callback copies only up to 26 header bytes and length/channel/time
metadata to a static 128-entry ring under portMUX_TYPE. Full-ring frames are
dropped. Poll drains at most 128 frames, ignores stale hop-channel frames,
parses in caller context, and hops allowed country channels every 350 ms while
searching. The country range is clamped to 1..14. A locked channel is held.

SC_TOUCH_FOUND_CHANNEL copies sc_capture_lock; SC_TOUCH_GOT_CREDENTIALS copies
sc_touch_result. Event posts use zero wait; errors propagate from poll without
marking delivery successful, so subsequent polls retry. A new lock after
timeout can produce a new channel-found event. Reception is disabled before
credentials are posted; the completed decoder persists until stop.

Stop disables ring acceptance before callback removal and decoder cleanup.
Channel/filter are restored on stop and start rollback. Restoration failures
propagate and preserve cleanup state so callers can retry stop; poll/start
reject a partially stopped instance. Deliberate exception: if the caller has
already associated after receiving credentials, stop leaves its live channel
intact. Stop before beginning association or after association completes;
the adapter does not manage an in-progress connection. Stop is safe inactive.
Start requires the default event loop as a caller precondition; event posting
reports an absent loop. Actual SDK compilation and OTA validation are separate
from host capture tests and must be performed on the target configuration.

The host IDF mock suite checks start refusals, rollback at each Wi-Fi API
failure point, failed event-post retries without duplicate delivery,
reception disabled before credentials, safe late callbacks after stop,
country-range hopping, association-safe channel handling and cleanup retry.
The stubs are authored from this contract and are only behavioral test
doubles, not copies of SDK declarations or evidence of SDK binary layouts.
The supervisor separately reports successful real IDF 6.0.3 builds; see
spec/SUPERVISOR-FEEDBACK-03.txt. Those host mocks do not establish hardware
interoperability; separately attributed hardware reports are described below.

AirKiss full-message mode
-------------------------
sc_airkiss.h provides an independent context with create/reset/destroy,
feed/get_result and a one-byte acknowledgment formatter. It implements full
unencrypted password || token || SSID messages, P=0..64, SSID=0..32 and
T=1..97. Bytes are counted, can include zero/high bits and are not C strings.
The special short-message magic symbol 8 is supported; first magic symbol 0
is deliberately rejected. No scan lookup or zero-padding of final blocks is
used. Each last block's CRC covers its actual byte count.

The decoder acquires complete magic and CRC-valid prefix quartets before
accepting indexed blocks. A sliding quartet and header pair recover from
lost or extra symbols. Valid blocks may arrive reordered or identically
repeated, and identical metadata may repeat between blocks. A valid
conflicting block or established metadata poisons the context until reset.
Malformed metadata, indices and CRCs do not commit partial credentials.
Without a scan hint, completion requires every block and the SSID CRC, then
freezes until reset. Plaintext scan hints may supply SSID as described above.
Returns are 0 incomplete, 1 complete, -1 NULL and -2 poisoned; getters leave
outputs untouched on failure. Reset clears results and preserves key configuration.
AirKiss context destruction and temporary plaintext clearing use volatile byte
writes; this does not guarantee erasure of caller copies or platform internals.

sc_airkiss_capture.h uses the same frame filtering, bounded four candidates,
source keys, retransmission policy, deadlines and lock type as sc_capture.h.
It recognizes one sliding quartet of distinct consecutive lengths (normalized
1..4) in any order/phase, at inferred overhead0..256.
MAC header/FCS length bounds still apply: overheads that make the tiny guide
frames shorter than a real MAC frame cannot acquire through capture. Its
normalized byte decoder has no such capture-length limitation. The message
has no BSSID; association BSSID comes exclusively from the capture lock.
SSID CRC does not authenticate that address. Conflicts unlock to search.

idf/sc_airkiss_idf.c provides SC_AIRKISS_EVENT and separate start/poll/stop
entry points with equivalent ring, hopping, cleanup and event-retry behavior.
FOUND_CHANNEL copies sc_capture_lock; GOT_CREDENTIALS copies
sc_airkiss_result. Retain the channel-event BSSID for association. After
association, sc_airkiss_make_ack returns the token byte; the caller transmits
it to UDP broadcast port 10000 and owns any repetition policy.

Call stop on one provisioning adapter before starting the other, including
after credentials arrive. The promiscuous-mode check rejects simultaneous
reception, but a completed adapter retains state after disabling reception;
that check is not a shared ownership mechanism for retained contexts.

The AirKiss behavioral input describes observations of a pinned public
community Android sender, not an official Tencent specification. Local tests
cover all 2145 permitted password/SSID length pairs, all total/tail sizes,
binary data, recovery, conflicts, capture and IDF lifecycle mocks. Supervisor
feedback in spec/SUPERVISOR-FEEDBACK-04.txt reports independent sender vectors
and fuzz/sanitizer checks. Binary-byte policy tests are an extension beyond
that sender's ASCII limitations. These checks do not establish official
conformance or compatibility with every phone. The supervisor subsequently
supplied spec/SUPERVISOR-FEEDBACK-05.txt as a newly recorded retrospective
hardware report: two controlled ASCII cases passed exact credentials, WPA2,
DHCP, UDP and token delivery. It documents different reference/new reply
lifecycles and limits the result to the tested frames and cases. This file
was not an earlier implementation input or an actual-phone conformance test.

Encrypted AirKiss (added in 0.1.1)
----------------------------------
The existing no-argument sc_airkiss_create/sc_airkiss_capture_create and
sc_airkiss_idf_start entry points remain plaintext. The additive
sc_airkiss_create_with_config/sc_airkiss_capture_create_with_config APIs
accept NULL for plaintext, or sc_airkiss_config with key[16], key_len (1..16),
decrypt and caller-owned user pointer. Configuration/key bytes are copied;
only the user pointer is borrowed and must remain valid through destruction.
Invalid non-NULL config returns NULL. Short raw keys are padded with zero
bytes to 16. The padded key is also the CBC IV, exactly as the supplied
behavioral contract specifies. This differs from both ESP-Touch v2 IV modes.

The callback returns 1 only after writing length decrypted bytes to the
separate output buffer, without removing padding. Length is a multiple of
16 from 16 through 80. The core checks every PKCS#7 suffix byte and publishes
only a password of 1..64 bytes. Strict sender policy rejects a padding-only
ciphertext; an empty password is represented by zero ciphertext bytes and
does not invoke decryption. Keyed message storage is bounded to 113 bytes
and 29 blocks; plaintext retains its previous 97-byte bound. Token/SSID stay
clear and their wire offsets count ciphertext bytes. Reset retains config;
a crypto failure stays incomplete until reset or capture timeout recovery.

sc_airkiss_idf_start_with_key(key, key_len) validates/copies 1..16 raw bytes
and uses the existing sc_touch2_psa_decrypt public-API CBC primitive. The
primitive handles up to 144 bytes for v2 and is also sufficient for AirKiss.
No AES implementation was added to the portable core. The example's distinct
CONFIG_SC_EXAMPLE_AIRKISS_KEY is 32 hex digits decoded to 16 raw bytes;
an empty setting invokes the original plaintext start. No wire encryption
flag exists, so both sides must explicitly select the same mode and key.
There is no plaintext fallback, authentication, or reliable wrong-key test:
a wrong key can accidentally produce accepted padding.

Owned tests cover 36 supplied native-crypto fixtures, callback failures,
all padding suffix checks, copied configuration, independent contexts,
metadata bounds, reordered blocks, empty/max fields, capture reset, and
keyed IDF failure/timeout/reacquisition behavior. make test-crypto separately
uses the standard Python cryptography AES implementation, verifies the 36
ciphertexts, decodes 12 supplied native sender sequences unchanged, and
checks binary/zero-key/padding/reset cases (262 actual decrypt calls). These
are host checks. The C spies and IDF/PSA mocks do not validate AES themselves.
The observed vintage sender wrapper allows passwords up to 32 bytes; 33..64
and binary strings are byte-codec extensions, not demonstrated phone support.
The earlier AirKiss hardware report covered cleartext only. Feedback09 now
records three encrypted-AirKiss cases, one keyed open-network case and one
wrong-key negative on two S3 boards. Actual-phone interoperability is untested.
The archived 0.1.0 release is unchanged. This paragraph describes the 0.1.1
extension evidence available before later standard-workflow work.

Separately, spec/SUPERVISOR-FEEDBACK-08.txt reports 1056 independently
executed native sender sequences passed core decoding/reset/config-copy
checks using real AES; 192 longer-password policy cases, 140 invalid cases,
and 8448 synthetic capture/header combinations also passed. These are host
simulations. All four selectable example configurations built on ESP-IDF
6.0.3 for ESP32-S3 and passed SmartConfig blob-exclusion link audits. The
supervisor completed the separate authorized hardware test, as recorded in
spec/SUPERVISOR-FEEDBACK-09.txt. Both original flash images were restored and
fully verified. The standalone example itself was not flashed.

ESP-Touch v2
------------
sc_touch2.h adds a separate normalized-length decoder. Its copied optional
config contains a 16-byte key, decryption callback and caller-owned user
pointer. Reset clears reception state but preserves that config. NULL config
supports plaintext; encrypted messages remain incomplete without a decrypt
callback. A callback must write exactly the requested number of decrypted
bytes and return 1; it must not remove padding. Cipher/plain buffers are
disjoint, length is a multiple of 16 and is at most 144 bytes.

The core validates the header CRC, lengths, supported protocol/security bits
and exact announced group count. Storage is bounded to 40 body groups.
Each eight-plane group requires indices 0..7; identical planes may repeat,
whereas a conflicting plane discards only the partial group. Indexed body
groups may be reordered or identically repeated. Valid changed committed
groups/headers and in-range count changes poison the session until reset.
Malformed framing and CRC failures never commit partial bytes.

Seven-bit groups carry six low-bit bytes and a six-bit CRC in the final
plane; eight-bit groups carry five full bytes plus a full CRC. The decoder
implements joined or separate plaintext password/reserved segments according
to header flags, and independently encoded SSID segments. Group padding is
checksummed and discarded. Result fields include counted SSID/password/
reserved bytes, lengths, port mark, security version, BSSID CRC and IPv4 flag.
No IP address is inferred or exposed by the flag.

Security version 1 uses a zero IV. Version 2 transports 20 IV bytes as four
groups and passes only the first 16 to decryption. Encrypted password and
reserved data share one ciphertext segment. Completion requires exact
plaintext length and every PKCS#7 padding byte to match the expected pad
length, then freezes the result until reset. CRC and padding checks do not
authenticate the sender or establish that a key is correct.

sc_touch2_capture.h uses the same MAC filters, candidate bounds, source keys,
sequence suppression and deadlines as the earlier adapters. Its guide is
L/M/L/M, with L=1048+overhead and M=L+23+count, overhead 0..256 and count
1..41. Each candidate checks a sliding four-length window, preserving valid
overlapping guides after a body plane that could itself resemble a guide
start. Lock primes core framing for immediately following header planes.
The decoded BSSID CRC must match the selected frame BSSID before completion;
mismatch/conflict clears capture state while retaining crypto configuration.
That CRC check is not BSSID authentication.

idf/sc_touch2_idf.c provides separate event/start/poll/stop APIs. Start takes
NULL for plaintext or a pointer to a 16-byte key, which is copied. Its
bounded ring, hopping, rollback, event retries, completion shutdown and stop
semantics match the other adapters. All adapters require stop before starting
another, even after completion. Add idf/sc_touch2_psa.c and an SDK dependency
providing psa/crypto.h when building v2 with the IDF adapter.

The separate PSA wrapper imports a volatile AES-128 key and uses CBC without
padding removal through the public multipart PSA Crypto API. It checks all
status returns, aborts operation state and destroys imported keys, and only
copies exact decrypted output after successful cleanup. No hand-written AES
or private crypto implementation headers are used. The portable host library
does not link PSA; callers provide their own callback. Ordinary portable
clearing is not a guaranteed secure erasure primitive.

sc_touch2_make_ack creates seven bytes: port mark then six-byte device MAC,
and returns destination port 18266+10000*mark. The caller broadcasts the UDP
reply after association and owns sockets/repetition. Pointer, size, mark and
security bounds are validated before modifying either output.

Local v2 tests cover 6400 plaintext size/encoding layouts, encrypted callback
plumbing and exact padding for both IV modes, malformed/missing/conflicting
planes, metadata bounds, capture and lifecycle. A preserved guide-overlap
regression additionally tests every 16-bit single-length prefix, lost guide
recovery and retransmission handling. PSA mocks test API arguments,
cleanup, failures and output bounds; callback spies and mocks are not AES
validation. Supervisor feedback in spec/SUPERVISOR-FEEDBACK-06.txt separately
reports independent Java-sender/Python-AES and capture checks. These local
tests are distinct from the following supervisor-reported hardware results.

Supervisor-reported v2 hardware validation
-----------------------------------------
spec/SUPERVISOR-FEEDBACK-07.txt reports all six controlled two-board rounds
passed after the guide-overlap repair: original and new receiver, each with
plaintext, security version 1 and security version 2. Every round recovered
exact synthetic credentials/reserved data, associated with the WPA2 lab AP,
obtained DHCP, exchanged UDP echo and delivered the expected seven-byte
port-mark/MAC reply. Marks 0/1/3 exercised ports 18266/28266/48266. New AES
modes used the actual target PSA wrapper. No premature result appeared during
the eight-second pre-transmission interval. The supervisor also reports
empty test NVS at each start, saved PHY calibration verification, and full
restoration and verification of both original flash images.

The report preserves the earlier failed plaintext hardware trial and
pre-repair host failure. After repair, independent supervisor checks passed
14,935 core assertions, 6,697 capture assertions, 1,508 assertions over all
377 entry points of three repeating streams, and two million sanitizer
probes. The implementation agent read only the permitted summary; it did
not inspect the supervisor's test logs, firmware or reference artifacts.

Channel finding is deliberately qualified: new plaintext/security1 events
reported capture channel 6, whereas security2 reported capture channel 10.
All measured overheads were 80. Security2 then associated successfully with
AP primary channel 6 in 40U mode. The cause is unproven. This observation does
not justify a code change or treating lock.channel as a guaranteed AP primary
channel; applications must not pin association to it.

These hardware observations cover one synthetic ASCII credentials/reserved
vector per mode and controlled unencrypted non-QoS From-DS frames with extra
synthetic length padding. Provisioning AES is independent of that radio
envelope. They do not establish actual-phone or encrypted AP-forwarded
interoperability, a complete vendor API/ABI replacement, or authentication
by CBC/CRC/padding. Vendor Wi-Fi/PHY still provide radio and association.
Nothing was published during those tests. That historical report predates
the user's 2026-10-08 selection of 0BSD for the new implementation; its source
text and recorded hash remain unchanged.

Remaining gaps
--------------
The optional adapters retain their earlier polling/capture behavior; standard
API discovery and fast-mode options are implemented by the compatibility worker. Association belongs
to the application; acknowledgment transmission belongs to the retained SDK
wrapper in the standard workflow, or to the optional extension application.
Phone orchestration, AirKiss discovery/control protocols, and crypto beyond
the specified v2/AirKiss callback/PSA paths remain outside this receiver. Fixed
ring overflow may lose packets. Source filtering is a bounded policy, not
authentication or guaranteed session separation. The standard internal
start/stop integration is explicitly implemented; undocumented private
layouts, other internal symbols and proprietary heuristics are not claimed.

Provenance
----------
PROVENANCE.json records the implementation's knowledge inputs. Only the local
AGENTS.md, permitted behavioral specifications and synthetic execution vectors,
recorded supervisor feedback, and permitted packaging documentation/tools were
read, along with the supplied public SmartConfig header/license, explicitly
permitted public PSA Crypto API header and
files created by this implementation. No sender source,
proprietary implementation, parent
workspace research, disassembly, credentials, or network was accessed.
This separation documents a development procedure; it is not a legal
certification or an opinion concerning licensing or intellectual property.
