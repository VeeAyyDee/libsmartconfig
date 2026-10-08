# Contributing

Start with a small change and a reproducible test. New project contributions
should be offered under the repository's 0BSD license. Identify any external
code and its license before proposing to include it; do not copy code from
an unlicensed or restricted reference into this implementation.

For decoder changes, include the exact non-private packet-length sequence and
expected output, then run the host tests and sanitizers. Preserve negative
tests and distinguish an independently sourced vector from a fixture written
to match the implementation. Passing one's own fixtures is not equivalence
proof. Keep API/contract changes and provenance notes explicit.

For IDF changes, build the affected example configurations and run the linked
SmartConfig-archive audit. Keep event callbacks bounded; the application task
owns adapter polling, stop, association and sockets. Do not log credentials.

When reporting interoperability, include SDK version, board, selected protocol,
phone/OS/app version, AP band/channel/security, and successes out of attempts.
Remove SSIDs, passwords, keys, MAC addresses and unrelated network data from
public logs. Do not submit device flash dumps or private captures.

The highest-value next contribution is a current Android sender with actual
phone-to-board evidence. See [the app handoff](docs/ANDROID-HANDOFF.md).
