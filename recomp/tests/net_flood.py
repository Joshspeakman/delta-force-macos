#!/usr/bin/env python3
"""net_flood.py <corpus> <from-copy> <to-copy> <seconds> <rate> [seed] [from-port]

Floods one copy of a game on the virtual test network (W32_NETVIRT, runtime/win32/winsock.c) with damaged datagrams:
what a malicious player (to a host) or a malicious host (to a joiner) could send. The corpus is made by a real game
with W32_NETCAPTURE=<file> (each datagram that arrived on the game port: a 16-bit length, then the bytes); it is data
from the game and stays on your computer. Run the target built with DF_SANITIZE=address (tools/build_runtime.sh) and
watch its log for AddressSanitizer reports and faults. from-port: the virtual port the datagrams seem to come from
(default 1568, i.e. the game port 3568; a joiner only takes datagrams from its host's address, any port).
"""
import random, socket, struct, sys, time

corpus, from_copy, to_copy, seconds, rate = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), float(sys.argv[4]), int(sys.argv[5])
rnd = random.Random(int(sys.argv[6]) if len(sys.argv) > 6 else 1)
from_port = int(sys.argv[7]) if len(sys.argv) > 7 else 1568
data, C, i = open(corpus, 'rb').read(), [], 0
while i + 2 <= len(data):
    n = struct.unpack_from('<H', data, i)[0]; C.append(data[i + 2:i + 2 + n]); i += 2 + n
real = lambda copy, vport: 30000 + 2000 * copy + vport % 2000          # winsock.c vreal()
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(('127.0.0.1', real(from_copy, from_port)))
dst = ('127.0.0.1', real(to_copy, 3568))

def damage(b):
    b = bytearray(b)
    for _ in range(rnd.randint(1, 6)):
        op = rnd.randrange(7)
        if op == 0 and b: b[rnd.randrange(len(b))] ^= 1 << rnd.randrange(8)               # a bit
        elif op == 1 and b: b[rnd.randrange(len(b))] = rnd.randrange(256)                 # a byte
        elif op == 2 and b: del b[rnd.randrange(len(b)):]                                 # cut short
        elif op == 3: b += bytes(rnd.randrange(256) for _ in range(rnd.randrange(1, 600)))   # lengthened
        elif op == 4 and len(b) >= 4:                                                     # a 32-bit count or length
            at = rnd.randrange(len(b) - 3); b[at:at + 4] = struct.pack('<I', rnd.choice([0xffffffff, 0x7fffffff, 0x80000000, 0x10000, rnd.getrandbits(32)]))
        elif op == 5 and len(b) >= 2:                                                     # a 16-bit one
            at = rnd.randrange(len(b) - 1); b[at:at + 2] = struct.pack('<H', rnd.choice([0xffff, 0x8000, 0x1000, rnd.getrandbits(16)]))
        elif op == 6: b = bytearray(rnd.choice(C))                                        # another datagram's start
    return bytes(b[:65000])

t0, sent = time.time(), 0
while time.time() - t0 < seconds:
    for _ in range(max(1, rate // 50)):
        s.sendto(damage(rnd.choice(C)), dst); sent += 1
    time.sleep(0.02)
print('sent', sent, 'damaged datagrams to copy', to_copy)
