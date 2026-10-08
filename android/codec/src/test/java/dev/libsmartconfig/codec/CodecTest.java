// SPDX-License-Identifier: 0BSD
package dev.libsmartconfig.codec;

import java.nio.file.Files;
import java.nio.file.Path;
import java.security.SecureRandom;
import java.util.Arrays;
import java.util.HexFormat;
import javax.crypto.Cipher;
import javax.crypto.spec.IvParameterSpec;
import javax.crypto.spec.SecretKeySpec;

public final class CodecTest {
    private static int checks;
    private static final byte[] MAC = {2,3,4,5,6,7}, IP = {(byte)192,(byte)168,4,2};
    private static final byte[] KEY = HexFormat.of().parseHex("000102030405060708090a0b0c0d0e0f");
    private static final SecureRandom ZERO = new SecureRandom() {
        @Override public void nextBytes(byte[] bytes) { Arrays.fill(bytes, (byte)0); }
    };
    private static void check(boolean ok, String label) { checks++; if (!ok) throw new AssertionError(label); }
    private static void invalid(Runnable r) {
        try { r.run(); throw new AssertionError("accepted invalid input"); } catch (IllegalArgumentException expected) { checks++; }
    }
    private static byte[] bytes(int n, boolean high) {
        byte[] b = new byte[n]; for (int i=0;i<n;i++) b[i]=(byte)((i%90)+ (high ? 128 : 32)); return b;
    }
    private static byte[] group(int[] lengths, int offset) {
        byte[] b = new byte[6];
        for (int k=0;k<6;k++) for(int i=0;i<8;i++) b[k] |= (byte)(((lengths[offset+i] >> (5-k)) & 1) << i);
        return b;
    }
    private static byte[] fullSegment(int[] lengths, int first, int size) {
        byte[] b = new byte[size];
        for(int offset=0;offset<size;offset+=5) {
            byte[] g=group(lengths, 15+(first+offset/5)*11);
            check((g[5]&255)==Touch2Encoder.crc(Arrays.copyOf(g,5)), "full group CRC");
            System.arraycopy(g,0,b,offset,Math.min(5,size-offset));
        }
        return b;
    }
    public static void main(String[] args) throws Exception {
        check(Touch2Encoder.crc(Touch2Encoder.utf8("123456789")) == 0xa1, "CRC-8/MAXIM check value");
        check(Touch2Encoder.utf8("Сеть").length == 8, "UTF-8 byte count");
        check(Touch2Encoder.utf8(" a ").length == 3, "no trim");
        invalid(() -> Touch2Encoder.utf8("\ud800"));
        invalid(() -> Touch2Encoder.key("0011"));
        invalid(() -> Touch2Encoder.key("z".repeat(32)));
        invalid(() -> Touch2Encoder.mac("02:00:00:00:00:00"));
        invalid(() -> Touch2Encoder.mac("ff:ff:ff:ff:ff:ff"));
        for(int n : new int[]{0,33}) invalid(() -> Touch2Encoder.encode(new byte[n],new byte[0],new byte[0],MAC,0,null,0,ZERO));
        invalid(() -> Touch2Encoder.encode(new byte[1],new byte[65],new byte[0],MAC,0,null,0,ZERO));
        invalid(() -> Touch2Encoder.encode(new byte[1],new byte[0],new byte[65],MAC,0,null,0,ZERO));
        invalid(() -> Touch2Encoder.encode(new byte[1],new byte[1],new byte[0],MAC,2,null,0,ZERO));
        invalid(() -> Touch2Encoder.encode(new byte[1],new byte[0],new byte[0],MAC,0,null,4,ZERO));
        invalid(() -> LegacyEncoder.touch1(new byte[33],new byte[0],MAC,IP));
        invalid(() -> LegacyEncoder.airkiss(new byte[1],new byte[65],0,null));
        invalid(() -> LegacyEncoder.airkiss(new byte[1],new byte[1],0,new byte[15]));
        int[] freshA=Touch2Encoder.encode(new byte[]{1},new byte[]{2},new byte[0],MAC,2,KEY,0,new SecureRandom());
        int[] freshB=Touch2Encoder.encode(new byte[]{1},new byte[]{2},new byte[0],MAC,2,KEY,0,new SecureRandom());
        check(!Arrays.equals(fullSegment(freshA,4,20),fullSegment(freshB,4,20)),"fresh transmitted security2 IV");
        int[] simple = Touch2Encoder.encode(new byte[]{0},new byte[0],new byte[0],MAC,0,null,0,ZERO);
        // One all-zero 7-bit body group: marker repeated, all seven planes and CRC plane have d=0.
        check(Arrays.equals(Arrays.copyOfRange(simple,12,23),new int[]{128,128,128,64,192,320,448,576,704,832,960}), "exact zero group lengths");
        check(Arrays.equals(Arrays.copyOf(simple,4),new int[]{1048,1073,1048,1073}), "exact v2 guide/count");
        int[] one = LegacyEncoder.touch1(new byte[]{0},new byte[0],MAC,IP);
        check(Arrays.equals(Arrays.copyOf(one,8),new int[]{515,514,513,512,515,514,513,512}), "v1 guide");
        // Indexed zero byte (SSID at index 9): checksum over 00 09 is 9c -> symbols 90,109,c0 +40.
        check(Arrays.equals(Arrays.copyOfRange(one,35,38),new int[]{184,305,232}), "v1 exact triplet");
        int[] air = LegacyEncoder.airkiss(new byte[]{0},new byte[0],0,null);
        check(Arrays.equals(air,new int[]{1,2,3,4,1,2,3,4,8,18,32,48,64,80,96,112,128,128,256,256}), "AirKiss zero message exact lengths");
        for(int security=1;security<=2;security++) for(int size : new int[]{1,5,6,15,16,17,31,32,63,64}) {
            byte[] p=bytes(size,true), r=bytes(7,false);
            int[] encoded=Touch2Encoder.encode(bytes(6,false),p,r,MAC,security,KEY,3,ZERO);
            byte[] header=group(encoded,4);
            check((header[4]&255)==(1|(security<<1)|24), "security/mark flags");
            int cipherSize=16*((p.length+r.length)/16+1), groups=(cipherSize+4)/5;
            byte[] cipher=fullSegment(encoded,0,cipherSize);
            byte[] iv=security==1 ? new byte[16] : Arrays.copyOf(fullSegment(encoded,groups,20),16);
            Cipher aes=Cipher.getInstance("AES/CBC/PKCS5Padding");
            aes.init(Cipher.DECRYPT_MODE,new SecretKeySpec(KEY,"AES"),new IvParameterSpec(iv));
            byte[] plain=aes.doFinal(cipher);
            check(Arrays.equals(Arrays.copyOf(plain,p.length),p), "AES password");
            check(Arrays.equals(Arrays.copyOfRange(plain,p.length,plain.length),r), "AES reserved");
            check(encoded[1]==1072+groups+(security==2?4:0)+1, "AES/group padding independent");
        }
        byte[] ack={2,2,3,4,5,6,7};
        check(Acknowledgement.parse(ack,7,2)!=null,"v2 ack");
        check(Acknowledgement.parse(ack,7,1)==null,"wrong mark");
        check(Acknowledgement.parse(new byte[8],8,0)==null,"overlong ack");
        check(Acknowledgement.parse(new byte[7],7,0)==null,"zero MAC");
        check(Acknowledgement.parse(Protocol.AIRKISS,new byte[]{42},1,42,IP)!=null,"token ack");
        check(Acknowledgement.parse(Protocol.AIRKISS,new byte[]{43},1,42,IP)==null,"wrong token");
        byte[] ack1={10,2,3,4,5,6,7,(byte)192,(byte)168,4,2};
        check(Acknowledgement.parse(Protocol.TOUCH1,ack1,11,10,IP)!=null,"v1 ack");
        check(Acknowledgement.parse(Protocol.TOUCH1,ack1,11,11,IP)==null,"v1 wrong total");
        check(Acknowledgement.parse(Protocol.TOUCH1,ack1,11,10,new byte[4])==null,"v1 source mismatch");
        Path output=Path.of(args.length==0 ? "build/vectors.tsv" : args[0]);
        Files.createDirectories(output.toAbsolutePath().getParent());
        StringBuilder vectors=new StringBuilder(); int cases=0;
        for(boolean high : new boolean[]{false,true}) for(int s : new int[]{1,5,6,31,32}) for(int p : new int[]{0,1,5,6,15,16,17,32,63,64}) {
            byte[] ss=bytes(s,high), pp=bytes(p,high);
            emit(vectors,"v1",0,ss,pp,new byte[0],null,10,LegacyEncoder.touch1(ss,pp,MAC,IP)); cases++;
            for(int sec=0;sec<2;sec++) { emit(vectors,"air",sec,ss,pp,new byte[0],sec==0?null:KEY,42,LegacyEncoder.airkiss(ss,pp,42,sec==0?null:KEY)); cases++; }
            for(int sec=0;sec<3;sec++) for(int r : new int[]{0,1,5,6,16,64}) {
                byte[] rr=bytes(r,!high);
                emit(vectors,"v2",p+r==0?0:sec,ss,pp,rr,sec==0?null:KEY,3,Touch2Encoder.encode(ss,pp,rr,MAC,sec,sec==0?null:KEY,3,new SecureRandom())); cases++;
            }
        }
        Files.writeString(output,vectors);
        System.out.println("PASS: "+checks+" encoder/ACK checks; exported "+cases+" synthetic receiver compatibility vectors");
    }
    private static void emit(StringBuilder out,String protocol,int sec,byte[] s,byte[] p,byte[] r,byte[] key,int match,int[] lengths) {
        HexFormat h=HexFormat.of();
        out.append(protocol).append('\t').append(sec).append('\t').append(h.formatHex(s)).append('\t').append(h.formatHex(p)).append('\t')
            .append(h.formatHex(r)).append('\t').append(key==null?"":h.formatHex(key)).append('\t').append(match).append('\t');
        for(int i=0;i<lengths.length;i++) { if(i>0)out.append(','); out.append(lengths[i]); } out.append('\n');
    }
}
