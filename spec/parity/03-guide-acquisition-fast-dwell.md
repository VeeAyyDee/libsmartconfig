# ESPTouch HT20 acquisition under50ms dwell

This supersedes the **chosen two-complete-cycle/eight-guide policy** in CAPTURE-V1 for the standard parity receiver. It is based on fresh inspection and original-only execution of the pinned libsmartconfig HT20 guide detector and its completion helper. No fresh capture/runtime source was read.

## Established original behavior

The original keeps a sliding four-length history per discovered AP and traffic direction. One valid quartet is enough for a guide candidate: four distinct lengths spanning exactly3, in any permutation. Equivalently, after subtracting an unknown constant overhead the set is {512,513,514,515}. It does not require two successive cycles, descending order, or seeing515 first. The detected overhead is minimum wire length minus512. Packet-direction/AP matching, receive-length range and retransmission checks still apply.

For ToDS acquisition, one valid quartet commits immediately: disarm/delete channel timer, clear hopping-active state, select scanned AP channel/secondary channel, initialize decoder/retry timers, then post FOUND_CHANNEL when this is the first lock. The fourth unique eligible guide frame suffices.

For FromDS acquisition, a valid quartet first creates a channel candidate with measured overhead/RSSI/AP identity. If RSSI>-60dBm, or the discovery candidate-channel count is exactly1, it commits immediately through the completion helper, stops hopping and emits FOUND_CHANNEL. If RSSI<=-60 and multiple candidate channels exist, it stores the candidate and advances survey. It marks the first candidate channel; after surveying back through that channel and leaving it, the retained candidates are compared by RSSI and the selected candidate commits. This is candidate survey after a complete quartet, not a requirement for eight guides in one dwell.

No timer extension/channel hold was observed merely from one,two or three ordinary guide lengths. A partial window by itself does not suspend hopping. Length histories reside with AP metadata; the ordinary next-channel timer operation does not clear them. The exact weak-signal multichannel survey and history aging behavior has not been reproduced in a full driver-loop oracle. Do not claim a missing guide can be reconstructed; instead a later complete four-sample window can acquire, including a rotated repeated cycle. Existing bounded candidate/source-expiry and strict fresh-frame ownership remain appropriate explicit implementation policies.

Original raw HT20 detector has wire-length acceptance bounds574..640 inclusive for this pinned build (`g_sync_len_min=574`, maximum640), and derives overhead relative to512. Earlier portable contract allows overhead0..256; that is a wider chosen portability range, not the original detector's raw wire filter. Do not mix UDP lengths with original raw receive lengths. The normalized guide condition itself is independent of overhead and direction.

## Original-only observations supplied in guide-acquisition-vectors.json

Frames used valid scanned AP identity, constant wire overhead100, unique12-bit sequence numbers, and no QoS. Each case records state after each frame, with explicit SDK timer/event mocks. With ToDS or FromDS RSSI-40dBm and3 candidate channels:

- [515,514,513,512] => no FOUND after1..3; one FOUND on4; hopper disabled.
- [513,512,515,514] => same result on4 (rotated cycle).
- [512,515,513,514] => same result on4 (permutation).
- [515,514,514,512] => no FOUND; hopper remains enabled.

FromDS RSSI-60dBm with3 channels stores a candidate and requests one survey hop on4; no immediate FOUND. The sameRSSI with1 channel commits on4. This verifies both the strict RSSI threshold and the one-channel shortcut. Decoder allocation/initialization and SDK services are modeled; original detection arithmetic and completion helper execute directly. No hardware timing, alternate decoder or fresh implementation was used to manufacture expected results.

## Actionable fresh behavior and tests

Replace the standard receiver's eight-guide gate with a sliding four-sample same-candidate test: unique lengths, maximum-minimum==3, inferred overhead valid. Accept any order/phase. Preserve candidate key separation, received frame validation, sequence retransmission rejection, measured overhead and subsequent full credential checks. Packet4 can immediately signal a confirmed candidate to the owning worker; that worker must freeze hopping before the next scheduled hop, then emit FOUND_CHANNEL before any credential event. Completion of a guide quartet is not credential authentication.

Immediate commitment on every valid same-source quartet is a reasonable bounded compatibility choice for the current receiver; for weak FromDS/multiple channels it differs from the original survey preference and should be identified as such. Implementing original weak-signal survey is optional for closing the reported strong controlled case, but do not call it fully identical vendor selection.

Do not solve the eight-guide problem by changing fast mode back to100ms or by indefinitely holding on one/two guide-like packets. Keep explicit fast-mode50ms request. Four frames spaced10ms need30ms from first to fourth; an arbitrary phase of the periodic sequence still fits in roughly40ms observation. An eight-frame requirement consumes70ms and can exceed the entire50ms dwell. This explains a plausible acquisition starvation mechanism but does not by itself prove the complete hardware failure cause: task/ring latency, driver retune delay and frame loss must still be measured.

Tests must cover one quartet, all four cycle rotations, arbitrary permutation, duplicate lengths, incomplete windows, different AP/sender/direction/channel candidates, duplicate sequence frames, and recovery after a dropped guide followed by a complete quartet. Add a virtual50ms hopper/10ms sender test with several scanned channels and changing initial phase; ensure one acquired quartet freezes hopping and eventually delivers omitted-SSID credentials using the scan cache. Keep SCAN_DONE before FOUND_CHANNEL and exactly one FOUND per lock. Run the targeted controlled hardware regression afterward; avoid extending claims to all RSSI/channel/interference conditions.
