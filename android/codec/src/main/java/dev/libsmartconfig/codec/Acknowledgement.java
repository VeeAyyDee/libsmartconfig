// SPDX-License-Identifier: 0BSD
package dev.libsmartconfig.codec;
import java.util.Arrays;
import java.util.Locale;

public final class Acknowledgement {
    private Acknowledgement() {}
    /** Null for malformed/mismatched replies. AirKiss carries no device MAC. */
    public static String parse(Protocol protocol, byte[] data, int length, int match, byte[] sourceIp) {
        if (data == null || length < 0 || data.length < length) return null;
        if (protocol == Protocol.AIRKISS) return length == 1 && (data[0] & 255) == match ? "Token matched · device MAC unavailable in AirKiss replies" : null;
        if (protocol == Protocol.TOUCH2) return parse(data, length, match);
        if (length != 11 || (data[0] & 255) != match || sourceIp == null || sourceIp.length != 4
            || !Arrays.equals(Arrays.copyOfRange(data, 7, 11), sourceIp)) return null;
        byte[] shortened = Arrays.copyOf(data, 7); shortened[0] = 0;
        return parse(shortened, 7, 0);
    }
    public static String parse(byte[] data, int length, int mark) {
        if (data == null || mark < 0 || mark > 3 || length != 7 || data.length < length || (data[0] & 255) != mark) return null;
        byte[] mac = Arrays.copyOfRange(data, 1, 7);
        if (!Touch2Encoder.plausibleMac(mac)) return null;
        StringBuilder s = new StringBuilder();
        for (byte b : mac) { if (s.length() != 0) s.append(':'); s.append(String.format(Locale.ROOT, "%02X", b & 255)); }
        return s.toString();
    }
}
