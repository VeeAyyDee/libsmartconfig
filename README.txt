sc_touch: portable C11 ESP-Touch v1/v2 and full-message AirKiss decoders

This library implements the behavioral contracts in spec/ESPTOUCH-V1.txt and
spec/CAPTURE-V1.txt, spec/AIRKISS-V1.txt and spec/ESPTOUCH-V2.txt.
Its sc_touch API is original and is not a vendor private ABI or an
ABI-compatible replacement for libsmartconfig.a. No full SmartConfig
compatibility claim is made. The user selected 0BSD on 2026-10-08 for this
new implementation and example; see LICENSE. This does not relicense SDK or
other third-party code.

Build and test
--------------
  make                 Build libsc_touch.a and libsc_touch.so.
  make test            Build/run strict warnings-as-errors unit tests.
  make sanitize        Run tests with AddressSanitizer and UBSan.
  make test-idf        Run owned IDF lifecycle and PSA wrapper mocks.
  make sanitize-idf    Run those mocks with ASAN and UBSan.
  make clean           Remove local build products.

Under a traced environment where LeakSanitizer cannot operate, use
  make sanitize ASAN_OPTIONS=detect_leaks=0
This still runs AddressSanitizer and UBSan, but does not check for leaks.

CC, AR, CPPFLAGS, CFLAGS and LDFLAGS may be overridden. The source uses only
standard C11 library facilities; Makefile shared-library/PIC/sanitizer flags
target toolchains supporting the usual Unix compiler options.

ESP-IDF component and example
-----------------------------
The root CMakeLists.txt registers all six portable and four IDF source files
with public include/ and idf/ directories. idf_component.yml declares version
0.1.0, license 0BSD, ESP-IDF >=6.0,<7.0 and ESP32-S3, the tested SDK family and target.
Required SDK components are esp_wifi, esp_event, esp_timer and mbedtls.
Use this directory as a local project component, or add it through your
project's EXTRA_COMPONENT_DIRS. The host Makefile remains independent of IDF.

The original example in examples/provision selects ESP-Touch v2 by default,
or ESP-Touch v1/AirKiss through its Kconfig choice. From that directory in an
activated IDF environment:

  idf.py set-target esp32s3
  idf.py menuconfig
  idf.py build

The project produces cleanroom_provision_example.elf. Its component dependency
is derived from the package root folder, so renaming this package to
sc_provision works. No absolute workstation path or remote component
dependency is needed. Optional user-operated flashing is:

  idf.py -p PORT flash monitor

See examples/provision/README.txt for Kconfig symbols, optional 32-hex-digit
v2 AES key, bounded session/retry behavior and protocol-specific replies.
A key stored in sdkconfig is embedded in firmware; it is not secure key
storage. This example uses IPv4 DHCP/UDP and rejects IPv6-marked v2 messages.
It stops capture before association, uses RAM-only credential configuration,
does not erase NVS on initialization errors, and does not pin association to
the observed capture channel. Event callbacks only enqueue bounded copies;
the application task polls and owns association and sockets.

Replace the vendor provisioning workflow with the selected original
sc_touch_idf_*, sc_touch2_idf_* or sc_airkiss_idf_* functions and matching
FOUND_CHANNEL/GOT_CREDENTIALS events. The example does not call or implement
esp_smartconfig APIs. Replacing this provisioning implementation does not
replace the vendor Wi-Fi/PHY radio stack. Earlier hardware validation used
separate harnesses; this new example itself has not been hardware tested.
Successful UDP sends mean local socket acceptance, not confirmed phone
receipt or a complete vendor acknowledgment/discovery lifecycle.

Use
---
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
0..32, all T+6 required indices, whole-message XOR, SSID CRC, and BSSID CRC.
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

Four bounded candidates recognize two complete 515/514/513/512 guide cycles
with inferred overhead 0..256. Candidate keys include BSSID, sender, channel,
DS direction, QoS and Protected. Candidates expire after 1500 ms without a
new eligible-header frame for their key. Replacement chooses an unused slot,
otherwise the least recently updated slot, with lowest-slot tie breaking.
Consecutive duplicate Sequence Control values do not advance or refresh it.
Other keys do not alter a retained candidate's sequence. Same-key mismatches
restart from the current length when it is a possible initial guide length.

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
Completion requires every block and the SSID CRC, then freezes until reset.
Returns are 0 incomplete, 1 complete, -1 NULL and -2 poisoned; getters leave
outputs untouched on failure. Reset/destroy use ordinary portable clearing,
with the same secure-erasure limitation as the ESP-Touch context.

sc_airkiss_capture.h uses the same frame filtering, bounded four candidates,
source keys, retransmission policy, deadlines and lock type as sc_capture.h.
It recognizes two ascending 1/2/3/4 guide cycles at inferred overhead 0..256.
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
Scanning/AP matching, automatic association, acknowledgment transmission,
phone orchestration, AirKiss AES and discovery/control protocols, and crypto
beyond the specified v2 callback/PSA path remain outside this library. Fixed ring overflow may lose
packets. Guide/source filtering is a chosen bounded policy, not authentication
or guaranteed session separation. No proprietary receiver heuristics, private
layouts, symbols, or internal API compatibility are implemented.

Provenance
----------
PROVENANCE.json records the implementation's knowledge inputs. Only the local
AGENTS.md, the permitted behavioral specifications, and recorded supervisor feedback were
read, along with the explicitly permitted public PSA Crypto API header and
files created by this implementation. No sender source,
proprietary implementation, parent
workspace research, disassembly, credentials, or network was accessed.
This separation documents a development procedure; it is not a legal
certification or an opinion concerning licensing or intellectual property.
