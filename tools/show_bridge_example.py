#!/usr/bin/env python3
"""POSIX serial bridge demo; standard library only, no BLE or show editor.

python3 tools/show_bridge_example.py SERIAL_PORT A1B2C3 D4E5F6
Save PC Bridge in Show; connect USB to PC before starting Link. Controller receive preferences must already be saved.
"""
import argparse
import os
import secrets
import select
import termios
import time


class Bridge:
    def __init__(self, port):
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        self.old = termios.tcgetattr(self.fd)
        mode = termios.tcgetattr(self.fd)
        mode[0] = mode[1] = mode[3] = 0
        mode[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        mode[4] = mode[5] = termios.B115200
        mode[6][termios.VMIN] = mode[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, mode)
        self.request_id = secrets.randbits(32)
        self.buffer = b''

    def close(self):
        termios.tcsetattr(self.fd, termios.TCSANOW, self.old)
        os.close(self.fd)

    def request(self, operation):
        self.request_id = (self.request_id + 1) & 0xFFFFFFFF
        prefix = f'NKSHOW 1 {self.request_id} '
        packet = (prefix + operation + '\n').encode('ascii')
        for _ in range(3):  # Retry exact bytes/ID, including relative deadlines.
            deadline = time.monotonic() + 4
            remaining = packet
            while remaining:
                if time.monotonic() >= deadline:
                    raise TimeoutError('USB write')
                if select.select([], [self.fd], [], .1)[1]:
                    remaining = remaining[os.write(self.fd, remaining):]
            while time.monotonic() < deadline:
                if select.select([self.fd], [], [], .1)[0]:
                    chunk = os.read(self.fd, 4096)
                    if not chunk:
                        raise OSError('USB disconnected')
                    self.buffer += chunk
                while b'\n' in self.buffer:
                    raw, self.buffer = self.buffer.split(b'\n', 1)
                    line = raw.decode('ascii', errors='replace').strip()
                    if not line.startswith(prefix):
                        continue  # Tab5 diagnostics can share its console.
                    print(line)
                    if not line.startswith(prefix + 'OK '):
                        raise RuntimeError(line)
                    return dict(word.split('=', 1) for word in line.split() if '=' in word)
                if len(self.buffer) > 8192:
                    raise RuntimeError('unterminated USB response')
        raise TimeoutError(operation)

    def wait_state(self, state, seconds=12):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if self.request('STATUS').get('state') == state:
                return
            time.sleep(.2)
        raise TimeoutError(state)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('port')
    parser.add_argument('controller_a')
    parser.add_argument('controller_b')
    args = parser.parse_args()
    for short_id in (args.controller_a, args.controller_b):
        if len(short_id) != 6 or any(c not in '0123456789abcdefABCDEF' for c in short_id):
            parser.error('controller IDs must contain exactly six hex digits')
    link = Bridge(args.port)
    try:
        link.request('HELLO')
        link.request('ARM')
        link.wait_state('READY')
        link.request('EVENT NOW ALL PATTERN 6')
        time.sleep(2)
        due = (int(link.request('TIME')['time']) + 1800) & 0xFFFFFFFF
        link.request(f'EVENT AT {due} SINGLE {args.controller_a} SOLID 255 0 0 255')
        link.request(f'EVENT AT {due} SINGLE {args.controller_b} SOLID 0 0 255 255')
        time.sleep(3)
        link.request('EVENT NOW ALL BLACKOUT')
        time.sleep(2)
        link.request('EVENT NOW ALL RELEASE')
        time.sleep(2)
    finally:
        try:
            link.request('DISARM')  # Drain future events, then ALL RELEASE and OFF.
            link.wait_state('OFF')
        finally:
            link.close()


if __name__ == '__main__':
    main()
