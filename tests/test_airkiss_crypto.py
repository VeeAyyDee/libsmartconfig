#!/usr/bin/env python3
# SPDX-License-Identifier: 0BSD
"""Owned AES integration test using permitted synthetic execution fixtures."""
import ctypes as c
import json
from pathlib import Path
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

ROOT = Path(__file__).resolve().parents[1]
lib = c.CDLL(str(ROOT / 'libsc_touch.so'))
U8 = c.c_uint8
P8 = c.POINTER(U8)
Decrypt = c.CFUNCTYPE(c.c_int, c.c_void_p, P8, P8, P8, c.c_size_t, P8)


class Config(c.Structure):
    _fields_ = [('key', U8 * 16), ('key_len', c.c_size_t),
                ('decrypt', Decrypt), ('user', c.c_void_p)]


class Result(c.Structure):
    _fields_ = [('ssid', U8 * 32), ('password', U8 * 64),
                ('ssid_len', U8), ('password_len', U8), ('token', U8)]


lib.sc_airkiss_create_with_config.argtypes = [c.POINTER(Config)]
lib.sc_airkiss_create_with_config.restype = c.c_void_p
lib.sc_airkiss_feed.argtypes = [c.c_void_p, c.c_uint16]
lib.sc_airkiss_get_result.argtypes = [c.c_void_p, c.POINTER(Result)]
lib.sc_airkiss_reset.argtypes = [c.c_void_p]
lib.sc_airkiss_destroy.argtypes = [c.c_void_p]
errors = []
calls = 0


@Decrypt
def decrypt(user, key, iv, ciphertext, length, output):
    global calls
    try:
        calls += 1
        key_bytes = c.string_at(key, 16)
        iv_bytes = c.string_at(iv, 16)
        assert key_bytes == iv_bytes and 16 <= length <= 80 and length % 16 == 0
        operation = Cipher(algorithms.AES(key_bytes), modes.CBC(iv_bytes)).decryptor()
        plain = operation.update(c.string_at(ciphertext, length)) + operation.finalize()
        c.memmove(output, plain, len(plain))
        return 1
    except Exception as error:
        errors.append(error)
        return 0


def encrypt(key, plain):
    padded_key = key.ljust(16, b'\0')
    operation = Cipher(algorithms.AES(padded_key), modes.CBC(padded_key)).encryptor()
    return operation.update(plain) + operation.finalize()


def pad(password):
    count = 16 - len(password) % 16
    return password + bytes([count]) * count


def crc8(data):
    crc = 0
    for byte in data:
        for _ in range(8):
            mix = (crc ^ byte) & 1
            crc >>= 1
            if mix:
                crc ^= 0x8c
            byte >>= 1
    return crc


def encode(ciphertext, ssid, token=90):
    message = ciphertext + bytes([token]) + ssid
    total, plen = len(message), len(ciphertext)
    a, b = crc8(ssid), crc8(bytes([plen]))
    values = [total >> 4 if total >= 16 else 8, 16 + (total & 15),
              32 + (a >> 4), 48 + (a & 15), 64 + (plen >> 4),
              80 + (plen & 15), 96 + (b >> 4), 112 + (b & 15)]
    for index in reversed(range((total + 3) // 4)):
        block = message[4 * index:4 * index + 4]
        values += [128 + (crc8(bytes([index]) + block) & 127), 128 + index]
        values += [256 + byte for byte in block]
    return values


def check(key, lengths, ssid, password, success=True, token=90):
    config = Config((U8 * 16).from_buffer_copy(key.ljust(16, b'\0')), len(key), decrypt, None)
    ctx = lib.sc_airkiss_create_with_config(c.byref(config))
    assert ctx
    c.memset(c.byref(config), 0xff, c.sizeof(config))
    try:
        for _ in range(2):
            result = Result()
            for length in lengths:
                lib.sc_airkiss_feed(ctx, length)
            assert lib.sc_airkiss_get_result(ctx, c.byref(result)) == int(success)
            if success:
                assert bytes(result.ssid[:result.ssid_len]) == ssid
                assert bytes(result.password[:result.password_len]) == password
                assert result.token == token
            lib.sc_airkiss_reset(ctx)
    finally:
        lib.sc_airkiss_destroy(ctx)


vectors = json.loads((ROOT / 'spec/airkiss-crypto-vectors.json').read_text())
for vector in vectors:
    key, password, ciphertext = (bytes.fromhex(vector[x]) for x in ('key', 'password', 'ciphertext'))
    assert encrypt(key, pad(password)) == ciphertext
    for size in (0, 1, 32):
        ssid = bytes(range(size))
        check(key, encode(ciphertext, ssid), ssid, password)

native = json.loads((ROOT / 'spec/airkiss-native-sender-sample.json').read_text())
for vector in native:
    key, password, ssid = (bytes.fromhex(vector[x]) for x in ('key', 'password', 'ssid'))
    check(key, vector['lengths'], ssid, password, token=vector['token'])

# Selected binary policy extensions and all-zero key are independent of wrapper string limits.
for size in (0, 1, 15, 16, 17, 32, 33, 63, 64):
    key = bytes(16)
    password = bytes(range(size))
    ciphertext = encrypt(key, pad(password)) if size else b''
    check(key, encode(ciphertext, b'\0\xff'), b'\0\xff', password)

# Full transport CRC remains valid; only decrypted padding/content is invalid.
key = bytes(range(16))
for malformed in (bytes(16), b'A' * 15 + b'\x11', b'A' * 12 + b'\x03\x04\x04\x04',
                  bytes([16]) * 16, b'A' * 65 + bytes([15]) * 15):
    check(key, encode(encrypt(key, malformed), b'x'), b'x', b'', success=False)
ciphertext = encrypt(key, pad(b'password'))
wrong_key = bytes(reversed(key))
check(wrong_key, encode(ciphertext, b'x'), b'x', b'', success=False)
assert not errors, errors
print(f'PASS: real host AES-CBC, {len(vectors)} crypto fixtures, {len(native)} native emitted sequences, binary/zero-key/padding/reset cases; {calls} decrypt calls')
