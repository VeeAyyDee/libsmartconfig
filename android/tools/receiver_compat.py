# SPDX-License-Identifier: 0BSD
"""Decode JVM-generated synthetic lengths using the actual portable C receivers.

Usage: python receiver_compat.py PATH_TO_SHARED_LIBRARY PATH_TO_vectors.tsv
Requires cryptography. The C receiver implementation is a black-box oracle here.
"""
import ctypes as C
import pathlib
import sys
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

U8 = C.c_uint8
DECRYPT = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(U8), C.POINTER(U8), C.POINTER(U8), C.c_size_t, C.POINTER(U8))

@DECRYPT
def decrypt(user, key, iv, cipher, length, plain):
    try:
        d = Cipher(algorithms.AES(bytes(key[:16])), modes.CBC(bytes(iv[:16]))).decryptor()
        result = d.update(bytes(cipher[:length])) + d.finalize()
        C.memmove(plain, result, length)
        return 1
    except Exception:
        return 0

class V2Config(C.Structure):
    _fields_ = [('key', U8*16), ('decrypt', DECRYPT), ('user', C.c_void_p)]
class AirConfig(C.Structure):
    _fields_ = [('key', U8*16), ('key_len', C.c_size_t), ('decrypt', DECRYPT), ('user', C.c_void_p)]
class V1(C.Structure):
    _fields_ = [('ssid', U8*32), ('password', U8*64), ('bssid', U8*6), ('ip', U8*4), ('ssid_len', U8), ('password_len', U8)]
class V2(C.Structure):
    _fields_ = [('ssid', U8*32), ('password', U8*64), ('reserved', U8*64)] + [(n,U8) for n in ('ssid_len','password_len','reserved_len','port_mark','security_version','bssid_crc','ipv4')]
class Air(C.Structure):
    _fields_ = [('ssid', U8*32), ('password', U8*64)] + [(n,U8) for n in ('ssid_len','password_len','token')]

lib = C.CDLL(str(pathlib.Path(sys.argv[1]).resolve()))
for prefix, result in [('sc_touch',V1), ('sc_touch2',V2), ('sc_airkiss',Air)]:
    getattr(lib,prefix+'_feed').argtypes = [C.c_void_p,C.c_uint16]
    getattr(lib,prefix+'_get_result').argtypes = [C.c_void_p,C.POINTER(result)]
    getattr(lib,prefix+'_destroy').argtypes = [C.c_void_p]
    getattr(lib,prefix+'_create').restype = C.c_void_p
lib.sc_touch_create.argtypes = []
lib.sc_touch2_create.argtypes = [C.POINTER(V2Config)]
lib.sc_airkiss_create_with_config.argtypes = [C.POINTER(AirConfig)]
lib.sc_airkiss_create_with_config.restype = C.c_void_p
counts = {'v1':0,'v2':0,'air':0}
for line_no,line in enumerate(pathlib.Path(sys.argv[2]).read_text().splitlines(),1):
    protocol,sec,s,p,r,k,match,lens = line.split('\t')
    s,p,r,k = map(bytes.fromhex,(s,p,r,k)); sec,match = int(sec),int(match)
    if protocol == 'v1':
        prefix,result,ctx='sc_touch',V1(),lib.sc_touch_create()
    elif protocol == 'v2':
        config=V2Config((U8*16).from_buffer_copy(k or bytes(16)),decrypt,None)
        prefix,result,ctx='sc_touch2',V2(),lib.sc_touch2_create(C.byref(config) if k else None)
    else:
        config=AirConfig((U8*16).from_buffer_copy(k or bytes(16)),16,decrypt,None)
        prefix,result,ctx='sc_airkiss',Air(),lib.sc_airkiss_create_with_config(C.byref(config) if k else None)
    assert ctx, (line_no,'allocation')
    try:
        for length in map(int,lens.split(',')):
            assert getattr(lib,prefix+'_feed')(ctx,length)>=0,(line_no,'conflict')
        assert getattr(lib,prefix+'_get_result')(ctx,C.byref(result))==1,(line_no,'incomplete')
        assert bytes(result.ssid[:result.ssid_len])==s,(line_no,'SSID')
        assert bytes(result.password[:result.password_len])==p,(line_no,'password')
        if protocol=='v2':
            assert bytes(result.reserved[:result.reserved_len])==r,(line_no,'reserved')
            assert result.security_version==sec and result.port_mark==match and result.ipv4==1
        if protocol=='air': assert result.token==match
        if protocol=='v1': assert bytes(result.bssid)==bytes([2,3,4,5,6,7]) and bytes(result.ip)==bytes([192,168,4,2])
        counts[protocol]+=1
    finally:
        getattr(lib,prefix+'_destroy')(ctx)
print('PASS: portable C receiver compatibility',counts,'total',sum(counts.values()))
