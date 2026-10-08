// SPDX-License-Identifier: 0BSD
package dev.libsmartconfig.codec;

import java.nio.ByteBuffer;
import java.nio.CharBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.security.GeneralSecurityException;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import javax.crypto.Cipher;
import javax.crypto.spec.IvParameterSpec;
import javax.crypto.spec.SecretKeySpec;

/** Original length encoder from spec/ESPTOUCH-V2.txt; no Android dependencies. */
public final class Touch2Encoder {
    private Touch2Encoder() {}

    public static byte[] utf8(CharSequence text) {
        try {
            ByteBuffer b = StandardCharsets.UTF_8.newEncoder()
                .onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT).encode(CharBuffer.wrap(text));
            byte[] result = new byte[b.remaining()]; b.get(result);
            if (b.hasArray()) Arrays.fill(b.array(), (byte) 0);
            return result;
        } catch (CharacterCodingException e) { throw new IllegalArgumentException("Invalid Unicode text"); }
    }

    public static byte[] key(String hex) {
        if (!hex.matches("[0-9a-fA-F]{32}"))
            throw new IllegalArgumentException("AES key must be exactly 32 hexadecimal digits");
        byte[] result = new byte[16];
        for (int i = 0; i < 16; i++) result[i] = (byte) Integer.parseInt(hex.substring(2*i, 2*i+2), 16);
        return result;
    }

    public static byte[] mac(String text) {
        if (!text.matches("(?i)[0-9a-f]{2}(:[0-9a-f]{2}){5}"))
            throw new IllegalArgumentException("BSSID must contain six colon-separated hex bytes");
        byte[] b = new byte[6];
        for (int i = 0; i < 6; i++) b[i] = (byte) Integer.parseInt(text.substring(3*i, 3*i+2), 16);
        if (!plausibleMac(b) || text.equalsIgnoreCase("02:00:00:00:00:00"))
            throw new IllegalArgumentException("Enter the real Wi-Fi BSSID; placeholder addresses are invalid");
        return b;
    }

    public static boolean plausibleMac(byte[] b) {
        if (b == null || b.length != 6 || (b[0] & 1) != 0) return false;
        int any = 0; for (byte v : b) any |= v;
        return any != 0;
    }

    public static int crc(byte[] bytes) {
        int c = 0;
        for (byte b : bytes) {
            c ^= b & 255;
            for (int i = 0; i < 8; i++) c = (c >>> 1) ^ ((c & 1) != 0 ? 0x8c : 0);
        }
        return c;
    }

    /** Caller owns input buffers and returned lengths and should erase them after use. */
    public static int[] encode(byte[] ssid, byte[] password, byte[] reserved, byte[] bssid,
                               int security, byte[] key, int mark, SecureRandom random) {
        if (ssid == null || ssid.length < 1 || ssid.length > 32 || password == null || password.length > 64
            || reserved == null || reserved.length > 64 || !plausibleMac(bssid) || mark < 0 || mark > 3
            || security < 0 || security > 2 || random == null)
            throw new IllegalArgumentException("SSID: 1–32 bytes; password/reserved: 0–64 bytes; mark: 0–3");
        if (security != 0 && (key == null || key.length != 16))
            throw new IllegalArgumentException("Encrypted mode requires a 16-byte key");
        if (password.length + reserved.length == 0) security = 0;
        List<int[]> groups = new ArrayList<>();
        boolean s8 = high(ssid), p8 = high(password), r8 = high(reserved);
        byte[] joined = new byte[password.length + reserved.length];
        System.arraycopy(password, 0, joined, 0, password.length);
        System.arraycopy(reserved, 0, joined, password.length, reserved.length);
        try {
            if (security == 0) {
                if (!p8 && !r8) segment(groups, joined, false, random);
                else { segment(groups, password, p8, random); segment(groups, reserved, r8, random); }
            } else {
                byte[] iv = new byte[20];
                if (security == 2) random.nextBytes(iv);
                byte[] cipher = null;
                try {
                    Cipher aes = Cipher.getInstance("AES/CBC/PKCS5Padding");
                    aes.init(Cipher.ENCRYPT_MODE, new SecretKeySpec(key, "AES"), new IvParameterSpec(iv, 0, 16));
                    cipher = aes.doFinal(joined);
                    segment(groups, cipher, true, random);
                    if (security == 2) segment(groups, iv, true, random);
                } catch (GeneralSecurityException e) { throw new IllegalStateException("AES unavailable", e); }
                finally { Arrays.fill(iv, (byte) 0); if (cipher != null) Arrays.fill(cipher, (byte) 0); }
            }
            segment(groups, ssid, s8, random);
            byte[] h = {(byte)(ssid.length | (s8 ? 128 : 0)), (byte)(password.length | (p8 ? 128 : 0)),
                (byte)(reserved.length | (r8 ? 128 : 0)), (byte)crc(bssid), (byte)(1 | security << 1 | mark << 3), 0};
            h[5] = (byte)crc(Arrays.copyOf(h, 5));
            int[] result = new int[12 + groups.size() * 11];
            result[0] = result[2] = 1048; result[1] = result[3] = 1072 + groups.size();
            System.arraycopy(planes(h), 0, result, 4, 8);
            for (int j = 0; j < groups.size(); j++) {
                int offset = 12 + j * 11;
                Arrays.fill(result, offset, offset + 3, 128 + j);
                System.arraycopy(groups.get(j), 0, result, offset + 3, 8);
            }
            return result;
        } finally {
            Arrays.fill(joined, (byte)0);
            for (int[] g : groups) Arrays.fill(g, 0);
        }
    }

    private static boolean high(byte[] b) { for (byte v : b) if (v < 0) return true; return false; }
    private static void segment(List<int[]> groups, byte[] bytes, boolean full, SecureRandom random) {
        int width = full ? 5 : 6;
        for (int offset = 0; offset < bytes.length; offset += width) {
            byte[] data = new byte[6]; random.nextBytes(data);
            if (!full) for (int i = 0; i < 6; i++) data[i] &= 127;
            System.arraycopy(bytes, offset, data, 0, Math.min(width, bytes.length - offset));
            if (full) data[5] = (byte)crc(Arrays.copyOf(data, 5));
            int[] p = planes(data);
            if (!full) p[7] = 64 | (7 << 7) | (crc(data) & 63);
            groups.add(p); Arrays.fill(data, (byte)0);
        }
    }
    private static int[] planes(byte[] b) {
        int[] p = new int[8];
        for (int i = 0; i < 8; i++) {
            int d = 0;
            for (int k = 0; k < 6; k++) d |= ((b[k] >>> i) & 1) << (5-k);
            p[i] = 64 | (i << 7) | d;
        }
        return p;
    }
}
