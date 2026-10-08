// SPDX-License-Identifier: 0BSD
package dev.libsmartconfig.codec;
public enum Protocol {
    TOUCH2("ESP-Touch v2", 0), TOUCH1("ESP-Touch v1", 18266), AIRKISS("AirKiss", 10000);
    public final String label;
    private final int port;
    Protocol(String label, int port) { this.label = label; this.port = port; }
    public int replyPort(int mark) { return this == TOUCH2 ? 18266 + 10000 * mark : port; }
}
