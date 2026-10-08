// SPDX-License-Identifier: 0BSD
package dev.libsmartconfig.codec;

import java.security.GeneralSecurityException;
import java.util.Arrays;
import javax.crypto.Cipher;
import javax.crypto.spec.IvParameterSpec;
import javax.crypto.spec.SecretKeySpec;

/** ESP-Touch v1 and full-message AirKiss, from the repository's behavioral specs. */
public final class LegacyEncoder {
    private LegacyEncoder() {}
    private static void validate(byte[] ssid, byte[] password) {
        if (ssid == null || ssid.length < 1 || ssid.length > 32 || password == null || password.length > 64)
            throw new IllegalArgumentException("SSID: 1–32 UTF-8 bytes; password: 0–64 UTF-8 bytes");
    }
    public static int[] touch1(byte[] ssid, byte[] password, byte[] bssid, byte[] ipv4) {
        validate(ssid, password);
        if (!Touch2Encoder.plausibleMac(bssid) || ipv4 == null || ipv4.length != 4)
            throw new IllegalArgumentException("ESP-Touch v1 requires a BSSID and sender IPv4 address");
        int total = 9 + ssid.length + password.length;
        byte[] data = new byte[total + 6];
        try {
            data[0] = (byte)total; data[1] = (byte)password.length;
            data[2] = (byte)Touch2Encoder.crc(ssid); data[3] = (byte)Touch2Encoder.crc(bssid);
            System.arraycopy(ipv4, 0, data, 5, 4);
            System.arraycopy(password, 0, data, 9, password.length);
            System.arraycopy(ssid, 0, data, 9 + password.length, ssid.length);
            for (int i = 0; i < total; i++) if (i != 4) data[4] ^= data[i];
            System.arraycopy(bssid, 0, data, total, 6);
            int[] result = new int[8 + 3 * data.length];
            for (int i = 0; i < 8; i++) result[i] = 515 - i % 4;
            for (int i = 0; i < data.length; i++) {
                int d = data[i] & 255, crc = Touch2Encoder.crc(new byte[]{data[i], (byte)i});
                result[8 + 3*i] = 40 + ((crc & 240) | (d >>> 4));
                result[9 + 3*i] = 296 + i;
                result[10 + 3*i] = 40 + ((crc & 15) << 4 | (d & 15));
            }
            return result;
        } finally { Arrays.fill(data, (byte)0); }
    }
    /** Key is exactly 16 raw bytes, identical to the example firmware's 32-hex-digit UI. */
    public static int[] airkiss(byte[] ssid, byte[] password, int token, byte[] key) {
        validate(ssid, password);
        if (token < 0 || token > 255 || (key != null && key.length != 16))
            throw new IllegalArgumentException("Invalid AirKiss token or AES key");
        byte[] wirePassword = password.clone(), data = null;
        try {
            if (key != null && password.length > 0) {
                Cipher aes = Cipher.getInstance("AES/CBC/PKCS5Padding");
                aes.init(Cipher.ENCRYPT_MODE, new SecretKeySpec(key, "AES"), new IvParameterSpec(key));
                Arrays.fill(wirePassword, (byte)0); wirePassword = aes.doFinal(password);
            }
            int total = wirePassword.length + 1 + ssid.length;
            data = new byte[total];
            System.arraycopy(wirePassword, 0, data, 0, wirePassword.length);
            data[wirePassword.length] = (byte)token;
            System.arraycopy(ssid, 0, data, wirePassword.length + 1, ssid.length);
            int[] result = new int[16 + total + 2 * ((total + 3) / 4)];
            for (int i = 0; i < 8; i++) result[i] = 1 + i % 4;
            int crc = Touch2Encoder.crc(ssid), pc = Touch2Encoder.crc(new byte[]{(byte)wirePassword.length});
            result[8] = total < 16 ? 8 : total >>> 4;
            result[9] = 0x10 | (total & 15); result[10] = 0x20 | crc >>> 4; result[11] = 0x30 | (crc & 15);
            result[12] = 0x40 | wirePassword.length >>> 4; result[13] = 0x50 | (wirePassword.length & 15);
            result[14] = 0x60 | pc >>> 4; result[15] = 0x70 | (pc & 15);
            int out = 16;
            for (int offset = 0; offset < total; offset += 4) {
                int count = Math.min(4, total-offset), index = offset/4;
                byte[] block = new byte[count+1]; block[0] = (byte)index;
                System.arraycopy(data, offset, block, 1, count);
                result[out++] = 128 | (Touch2Encoder.crc(block) & 127); result[out++] = 128 | index;
                for (int j = 0; j < count; j++) result[out++] = 256 | (data[offset+j] & 255);
                Arrays.fill(block, (byte)0);
            }
            return result;
        } catch (GeneralSecurityException e) { throw new IllegalStateException("AES unavailable", e); }
        finally { Arrays.fill(wirePassword, (byte)0); if (data != null) Arrays.fill(data, (byte)0); }
    }
}
