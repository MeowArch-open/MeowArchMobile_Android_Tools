#!/usr/bin/env python3
"""TCP fastboot mock with throttled ingest (~15 MB/s) to exercise the bar."""
import socket, struct, sys, time

HOST = "127.0.0.1"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 5555
RATE = 15 * 1024 * 1024  # bytes/sec ingest cap
srv = socket.socket(); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind((HOST, PORT)); srv.listen(1)
conn, _ = srv.accept(); conn.settimeout(60)

def recvn(n):
    buf = b""
    while len(buf) < n:
        d = conn.recv(n - len(buf))
        if not d: raise EOFError
        buf += d
    return buf

assert recvn(4) == b"FB01"
conn.sendall(b"FB01")

def send_msg(p): conn.sendall(struct.pack(">Q", len(p)) + p)
def read_msg():
    (n,) = struct.unpack(">Q", recvn(8)); return recvn(n)

try:
    while True:
        cmd = read_msg().decode(errors="replace")
        if cmd.startswith("getvar:"):
            name = cmd[7:]
            send_msg(b"OKAY0x10000000" if name == "max-download-size" else
                     (b"OKAYa" if name == "current-slot" else b"OKAY"))
        elif cmd.startswith("download:"):
            size = int(cmd[9:], 16)
            send_msg(b"DATA%08x" % size)
            got = 0; t0 = time.time()
            while got < size:
                chunk = read_msg(); got += len(chunk)
                elapsed = time.time() - t0
                target = got / RATE
                if target > elapsed: time.sleep(target - elapsed)
            send_msg(b"OKAY")
        elif cmd.startswith("flash:"):
            time.sleep(0.2); send_msg(b"OKAY")
        elif cmd.startswith("reboot"):
            send_msg(b"OKAY"); break
        else:
            send_msg(b"OKAY")
except EOFError:
    pass
finally:
    conn.close(); srv.close()
