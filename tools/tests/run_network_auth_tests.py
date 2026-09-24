#!/usr/bin/env python3
"""Host TCP/auth regressions; independent Wrath SRP peer, not PS4 acceptance.

Protocol reference: AzerothCore src/common/Cryptography/Authentication/SRP6.cpp
and src/server/apps/authserver/Server/AuthSession.cpp. Only synthetic accounts
are used; no server installation or client game data is required.
"""
import hashlib
import hmac
import os
from pathlib import Path
import select
import shlex
import socket
import struct
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
N = int('894B645E89E1535BBDAD5B8B290650530801B18EBFBF5E8FAB3C82872A3E9BB7', 16)
USER, PASSWORD = b'TESTACCOUNT', b'TESTPASSWORD'

def h(*parts):
    return hashlib.sha1(b''.join(parts)).digest()

def le(value):
    return value.to_bytes(32, 'little')

def integer(value):
    return int.from_bytes(value, 'little')

def session(a_wire, b_wire, b_secret, verifier):
    u = integer(h(a_wire, b_wire))
    secret = le(pow(integer(a_wire) * pow(verifier, u, N), b_secret, N))
    first = 0
    while first < len(secret) and secret[first] == 0:
        first += 1
    first += first % 2
    halves = h(secret[first::2]), h(secret[first + 1::2])
    key = bytes(byte for pair in zip(*halves) for byte in pair)
    return secret, key

def exact(connection, size):
    data = bytearray()
    while len(data) < size:
        chunk = connection.recv(size - len(data))
        if not chunk:
            raise AssertionError('client closed before complete packet')
        data.extend(chunk)
    return bytes(data)

def run_peer(binary, label, mode='plain', cut=32, small=False, short_b=False, zero_s=False):
    salt = bytes(range(1, 33)) if not small else bytes(range(1, 32)) + b'\0'
    verifier = pow(7, integer(h(salt, h(USER, b':', PASSWORD))), N)
    b_secret = 100
    while True:
        b_wire = le((3 * verifier + pow(7, b_secret, N)) % N)
        if short_b and b_wire[-1] != 0:
            b_secret += 1
            continue
        if zero_s and session(le(7), b_wire, b_secret, verifier)[0][0] != 0:
            b_secret += 1
            continue
        break
    env = dict(os.environ)
    if small:
        env['WOWPS_TEST_SMALL_A'] = '1'
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        listener.settimeout(10)
        process = subprocess.Popen([str(binary), str(listener.getsockname()[1]), mode],
                                   cwd=binary.parent, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            with listener.accept()[0] as conn:
                conn.settimeout(10)
                conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                header = exact(conn, 4)
                assert header[:2] == b'\0\x08'
                body = exact(conn, int.from_bytes(header[2:], 'little'))
                assert body[:4] == b'WoW\0' and body[4:7] == b'\x03\x03\x05'
                assert body[7:9] == struct.pack('<H', 12340)
                assert body[9:13] == b'68x\0' and body[13:17] == b'niW\0'
                assert body[29] == len(USER) and body[30:] == USER
                challenge = b'\0\0\0' + b_wire + b'\x01\x07\x20' + le(N) + salt + bytes(17)
                start = 0
                for end in (1, 35, 36, 37, 70, 102, 118, 119):
                    conn.sendall(challenge[start:end])
                    start = end
                    time.sleep(0.003)
                proof = exact(conn, 75)
                assert proof[0] == 1 and proof[73:] == bytes(2)
                a_wire, supplied_m1 = proof[1:33], proof[33:53]
                assert integer(a_wire) % N != 0
                if small:
                    assert a_wire == le(7)
                secret, key = session(a_wire, b_wire, b_secret, verifier)
                if zero_s:
                    assert secret[0] == 0
                n_xor_g = bytes(a ^ b for a, b in zip(h(le(N)), h(b'\x07')))
                m1 = h(n_xor_g, h(USER), salt, a_wire, b_wire, key)
                assert supplied_m1 == m1, f'{label}: SRP client proof differs from independent peer'
                if mode == 'reject':
                    conn.sendall(b'\x01\x04')
                    conn.shutdown(socket.SHUT_WR)
                else:
                    m2 = h(a_wire, m1, key) if mode != 'bad_m2' else bytes(20)
                    response = b'\x01\0' + m2 + struct.pack('<IIH', 0x800000, 0, 0)
                    conn.sendall(response[:cut])
                    if cut < len(response):
                        assert not select.select([conn], [], [], 0.04)[0], 'client processed incomplete proof'
                        conn.sendall(response[cut:])
                    if mode != 'bad_m2':
                        assert exact(conn, 5) == b'\x10\0\0\0\0'
                        realm = b'\0\0\0Test Realm\0' + b'127.0.0.1:8085\0' + struct.pack('<fBBB', 0.5, 1, 1, 1)
                        payload = struct.pack('<IH', 0, 1) + realm + b'\x10\0'
                        reply = b'\x10' + struct.pack('<H', len(payload)) + payload
                        for start in range(0, len(reply), 3):
                            conn.sendall(reply[start:start + 3])
                            time.sleep(0.001)
                output, errors = process.communicate(timeout=10)
                assert process.returncode == 0, (label, process.returncode, output.decode(), errors.decode())
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()
    print(f'PASS {binary.name}: {label}', flush=True)

class HeaderCipher:
    """Independent RC4 implementation for the protocol test peer."""
    def __init__(self, seed, key):
        digest = hmac.new(bytes.fromhex(seed), key, hashlib.sha1).digest()
        self.state = list(range(256))
        j = 0
        for i in range(256):
            j = (j + self.state[i] + digest[i % len(digest)]) % 256
            self.state[i], self.state[j] = self.state[j], self.state[i]
        self.i = self.j = 0
        self.process(bytes(1024))

    def process(self, data):
        result = bytearray()
        for byte in data:
            self.i = (self.i + 1) % 256
            self.j = (self.j + self.state[self.i]) % 256
            self.state[self.i], self.state[self.j] = self.state[self.j], self.state[self.i]
            result.append(byte ^ self.state[(self.state[self.i] + self.state[self.j]) % 256])
        return bytes(result)

def run_world_peer(binary, asynchronous):
    env = dict(os.environ, WOWEE_NET_ASYNC_PUMP=str(int(asynchronous)))
    key = bytes(range(40))
    incoming = HeaderCipher('c2b3723cc6aed9b5343c53ee2f4367ce', key)
    outgoing = HeaderCipher('cc98ae04e897eaca12ddc09342915357', key)
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        listener.settimeout(10)
        process = subprocess.Popen([str(binary), str(listener.getsockname()[1])],
                                   cwd=binary.parent, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            with listener.accept()[0] as conn:
                conn.settimeout(5)
                conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                header = struct.pack('>H', 42) + struct.pack('<H', 0x1ec)
                conn.sendall(header + struct.pack('<II', 1, 12345) + bytes(32))
                assert exact(conn, 6) == struct.pack('>H', 8) + struct.pack('<I', 0x1ed)
                assert exact(conn, 4) == struct.pack('<I', 12340)
                response = outgoing.process(struct.pack('>H', 3) + struct.pack('<H', 0x1ee)) + b'\x0c'
                conn.sendall(response)  # Arrives while the test send wrapper is delayed.
                assert incoming.process(exact(conn, 6)) == struct.pack('>H', 4) + struct.pack('<I', 0x37)
                response = outgoing.process(struct.pack('>H', 3) + struct.pack('<H', 0x3b)) + b'\0'
                for byte in response:
                    conn.sendall(bytes([byte]))
                    time.sleep(0.004)
                output, errors = process.communicate(timeout=8)
                assert process.returncode == 0, (process.returncode, output.decode(), errors.decode())
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()
    print(f'PASS world transport: immediate encrypted reply and fragmented header; async={asynchronous}', flush=True)

def main():
    compiler = shlex.split(os.environ.get('CXX', 'c++'))
    flags = ['-std=c++20', '-O1', '-g', '-pthread', '-I' + str(ROOT / 'include')]
    if os.environ.get('SANITIZE', '1') != '0':
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    os.environ.setdefault('ASAN_OPTIONS', 'detect_leaks=0')
    os.environ.setdefault('UBSAN_OPTIONS', 'halt_on_error=1')
    with tempfile.TemporaryDirectory(prefix='wowps-network-') as temporary:
        out = Path(temporary)
        common = ['src/network/tcp_socket.cpp', 'src/network/packet.cpp', 'src/core/logger.cpp']
        transport = out / 'transport'
        subprocess.run(compiler + flags + [str(ROOT / p) for p in common] +
                       [str(ROOT / 'tools/tests/network_transport_test.cpp'), '-Wl,--wrap=send',
                        '-Wl,--wrap=getsockopt', '-o', str(transport)], check=True)
        subprocess.run([str(transport)], cwd=out, check=True, timeout=30)
        world = out / 'world-transport'
        world_sources = ['src/network/world_socket.cpp', 'src/network/packet.cpp', 'src/core/logger.cpp',
                         'src/game/opcode_table.cpp', 'src/auth/crypto.cpp', 'src/auth/rc4.cpp', 'src/auth/vanilla_crypt.cpp',
                         'tools/tests/world_transport_client.cpp']
        subprocess.run(compiler + flags + [str(ROOT / p) for p in world_sources] +
                       ['-Wl,--wrap=send', '-lcrypto', '-o', str(world)], check=True)
        run_world_peer(world, False)
        run_world_peer(world, True)
        auth = common + ['src/auth/' + name + '.cpp' for name in
                         ('auth_handler', 'auth_packets', 'auth_opcodes', 'srp', 'big_num', 'crypto', 'pin_auth', 'integrity')]
        for shim in (False, True):
            binary = out / ('auth-ps4-crypto' if shim else 'auth-openssl')
            extra = ['-DWOWEE_CRYPTO_SHIM=1', '-I' + str(ROOT / 'ps4/compat')]
            extra += [str(p) for p in sorted((ROOT / 'ps4/compat/src').glob('*.cpp'))]
            subprocess.run(compiler + flags + [str(ROOT / p) for p in auth] +
                           [str(ROOT / 'tools/tests/azeroth_auth_client.cpp'), '-Wl,--wrap=RAND_bytes'] +
                           (extra if shim else ['-lcrypto']) + ['-o', str(binary)], check=True)
            run_peer(binary, 'normal login and realm list')
            for cut in (26, 28, 31):
                run_peer(binary, f'proof split at byte {cut}', cut=cut)
            run_peer(binary, 'padded A, B and salt', small=True, short_b=True)
            run_peer(binary, 'zero-prefix shared secret', small=True, zero_s=True)
            run_peer(binary, 'saved credential hash', mode='hash', small=True)
            run_peer(binary, 'account rejection stays terminal', mode='reject')
            run_peer(binary, 'invalid server proof rejected', mode='bad_m2')

if __name__ == '__main__':
    main()
