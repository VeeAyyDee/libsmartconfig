# AirKiss acquisition: one quartet, not two cycles

This supersedes inherited eight-guide/two-cycle capture policy for AirKiss standard parity reception. Original-only execution confirms the same mismatch addressed for ESPTouch in03.

Pinned original AirKiss direct and trans detectors keep four recent lengths per AP/direction. One quartet of four distinct consecutive lengths, in **any order**, yields a candidate. The normalized set is {1,2,3,4}; overhead=min(wire lengths)-1. One accepted quartet satisfies the candidate threshold. No second ascending cycle is required. Raw pinned detector range is64..125 bytes inclusive; normalized guide values must not be confused with that raw frame length range.

Direct/ToDS commits on packet4, stops hopping and emits FOUND_CHANNEL. Trans/FromDS commits after one quartet when RSSI>-60dBm, candidate channel count==1, or the session already acquired the other direction. Weak FromDS/multiple channels instead retains an RSSI-ranked candidate and advances channel survey, as in03. This weak-signal survey preference is separate from guide-count requirements. No hold/extension based solely on one,two or three guide packets was observed.

Original-only vectors in airkiss-guide-vectors.json execute the original dispatcher, both detectors and completion helper. Decoder initialization and external SDK services are explicit mocks. With overhead100, unique packet sequences and matching scanned AP identity:

- [1,2,3,4], [3,4,1,2] and [4,1,3,2] all stop hopping and emit one FOUND on packet4 for ToDS or FromDS RSSI-40 with3 channels.
- [1,2,2,4] does not lock.
- FromDS RSSI-60 with3 channels records a survey hop on4; one-channel discovery commits on4.

Apply the same sliding four-sample acquisition correction as03, changing the normalized base from512 to1. Keep source/direction/channel isolation, sequence duplicate filtering, bounded candidate lifetime, and downstream AirKiss magic/prefix/data CRC checks. Preserve plaintext and explicit-key paths; guide acquisition itself does not depend on the later payload encryption setting. Keep fast50ms dwell. Do not alter v2 acquisition based solely on this evidence.

Regression tests: one quartet, all phase rotations, permutation, duplicate length/sequence, wrong source, omittedSSID completion from validated scan cache, and several candidate channels at10ms guide spacing/50ms dwell. Include AirKiss-only and combined selection so an eight-guide gate is not retained in one path. Full weak-signal survey equivalence and real RF timing are not claimed; targeted hardware validation remains necessary.
