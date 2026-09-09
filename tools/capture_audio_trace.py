#!/usr/bin/env python3
"""Capture NightKite Audio V2 and Link DSP diagnostics using only the standard library."""

import argparse
import glob
import json
import math
import os
import secrets
import select
import statistics
import termios
import time


class SerialLines:
    def __init__(self, path):
        self.path = path
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        self.old = termios.tcgetattr(self.fd)
        mode = termios.tcgetattr(self.fd)
        mode[0] = mode[1] = mode[3] = 0
        mode[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        mode[4] = mode[5] = termios.B115200
        mode[6][termios.VMIN] = mode[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, mode)
        self.buffer = b""

    def close(self):
        termios.tcsetattr(self.fd, termios.TCSANOW, self.old)
        os.close(self.fd)

    def write(self, data):
        remaining = data
        deadline = time.monotonic() + 2
        while remaining:
            if time.monotonic() >= deadline:
                raise TimeoutError((self.path, "write"))
            if select.select([], [self.fd], [], 0.1)[1]:
                remaining = remaining[os.write(self.fd, remaining):]

    def lines(self, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if select.select([self.fd], [], [], min(0.05, deadline - time.monotonic()))[0]:
                chunk = os.read(self.fd, 8192)
                if not chunk:
                    raise OSError((self.path, "disconnected"))
                self.buffer += chunk
            while b"\n" in self.buffer:
                raw, self.buffer = self.buffer.split(b"\n", 1)
                yield raw.decode("ascii", errors="replace").strip()


def fields(line):
    return dict(word.split("=", 1) for word in line.split() if "=" in word)


def numeric(value):
    try:
        return float(value) if "." in value else int(value)
    except ValueError:
        return value


class Controller:
    def __init__(self, path):
        self.serial = SerialLines(path)
        self.sequence = secrets.randbelow(1000000)

    @property
    def path(self):
        return self.serial.path

    def close(self):
        self.serial.close()

    def request(self, command, timeout=2):
        self.sequence += 1
        prefix = f"NK4 seq={self.sequence} "
        packet = (prefix + "cmd=" + command + "\n").encode("ascii")
        for _ in range(3):
            self.serial.write(packet)
            for line in self.serial.lines(timeout):
                while line.startswith("nk>"):
                    line = line[3:].lstrip()
                if line.startswith(prefix):
                    if not line.startswith(prefix + "ok"):
                        raise RuntimeError(line)
                    return fields(line)
        raise TimeoutError((self.path, command))


class ShowLink:
    def __init__(self, path):
        self.serial = SerialLines(path)
        self.request_id = secrets.randbits(32)
        self.diagnostics = []

    @property
    def path(self):
        return self.serial.path

    def close(self):
        self.serial.close()

    def consume(self, line):
        if line.startswith("audio: "):
            parsed = {key: numeric(value) for key, value in fields(line).items()}
            parsed["captured_at"] = time.monotonic()
            self.diagnostics.append(parsed)

    def request(self, operation, timeout=4):
        self.request_id = (self.request_id + 1) & 0xFFFFFFFF
        prefix = f"NKSHOW 1 {self.request_id} "
        packet = (prefix + operation + "\n").encode("ascii")
        for _ in range(3):
            self.serial.write(packet)
            for line in self.serial.lines(timeout):
                self.consume(line)
                if line.startswith(prefix):
                    if not line.startswith(prefix + "OK "):
                        raise RuntimeError(line)
                    return fields(line)
        raise TimeoutError((self.path, operation))

    def drain(self):
        for line in self.serial.lines(0.001):
            self.consume(line)

    def wait_state(self, expected, timeout=15):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.request("STATUS").get("state") == expected:
                return
            time.sleep(0.2)
        raise TimeoutError(expected)


def detect_devices(paths):
    link = None
    controllers = []
    for path in paths:
        try:
            controller = Controller(path)
            controller.request("info", 1.0)
            controllers.append(controller)
            continue
        except (OSError, RuntimeError, TimeoutError):
            try:
                controller.close()
            except (NameError, OSError):
                pass
        try:
            candidate = ShowLink(path)
            candidate.request("HELLO", 1.5)
            if link is not None:
                raise RuntimeError("multiple NightKite Link devices")
            link = candidate
        except (OSError, RuntimeError, TimeoutError):
            try:
                candidate.close()
            except (NameError, OSError):
                pass
    if link is None or not controllers:
        for device in controllers:
            device.close()
        if link is not None:
            link.close()
        raise RuntimeError(f"expected one Link and at least one controller, found paths={paths}")
    return link, controllers


def correlation(rows, first, second):
    pairs = [(float(row[first]), float(row[second])) for row in rows if first in row and second in row]
    if len(pairs) < 2:
        return None
    xs, ys = zip(*pairs)
    xmean, ymean = statistics.fmean(xs), statistics.fmean(ys)
    numerator = sum((x - xmean) * (y - ymean) for x, y in pairs)
    denominator = math.sqrt(sum((x - xmean) ** 2 for x in xs) * sum((y - ymean) ** 2 for y in ys))
    return numerator / denominator if denominator else None


def summarize(rows, started):
    valid = [row for row in rows if int(row.get("signal_valid", 0))]
    locked = [row for row in rows if int(row.get("beat_locked", 0))]
    periods = [int(row["audio_beat_ms"]) for row in locked if int(row.get("audio_beat_ms", 0))]
    first_lock = next((row["captured_at"] - started for row in rows if int(row.get("beat_locked", 0))), None)
    summary = {
        "samples": len(rows),
        "signal_valid_pct": 100.0 * len(valid) / len(rows) if rows else 0.0,
        "beat_locked_pct": 100.0 * len(locked) / len(rows) if rows else 0.0,
        "first_lock_s": first_lock,
        "median_beat_ms": statistics.median(periods) if periods else None,
        "median_bpm": 60000.0 / statistics.median(periods) if periods else None,
        "energy_bass_correlation": correlation(valid, "audio_energy", "audio_bass"),
        "energy_mid_correlation": correlation(valid, "audio_energy", "audio_mid"),
        "energy_treble_correlation": correlation(valid, "audio_energy", "audio_treble"),
    }
    for name in ("audio_energy", "audio_bass", "audio_mid", "audio_treble", "audio_confidence"):
        values = [int(row.get(name, 0)) for row in valid]
        summary[name + "_median"] = statistics.median(values) if values else None
        summary[name + "_max"] = max(values) if values else None
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--duration", type=float, default=30.0)
    parser.add_argument("--label", default="audio")
    parser.add_argument("--output", default="audio-trace.json")
    parser.add_argument("--pattern", type=int, default=23)
    parser.add_argument("ports", nargs="*", help="optional /dev/cu.* paths; otherwise USB modems are probed")
    args = parser.parse_args()
    paths = args.ports or sorted(glob.glob("/dev/cu.usbmodem*"))
    link, controllers = detect_devices(paths)
    rows = []
    dsp_status = []
    show_status = {}
    identities = [controller.request("info") for controller in controllers]
    try:
        link.request("ARM")
        link.wait_state("READY")
        link.request(f"EVENT NOW ALL PATTERN {args.pattern}")
        link.request("AUDIO MIC_FULL")
        started = time.monotonic()
        deadline = time.monotonic() + args.duration
        while time.monotonic() < deadline:
            for index, controller in enumerate(controllers):
                row = {key: numeric(value) for key, value in controller.request("audio_sync_status").items()}
                row.update(controller=index, captured_at=time.monotonic())
                rows.append(row)
            link.drain()
            time.sleep(0.08)
        status = {key: numeric(value) for key, value in link.request("AUDIO_STATUS").items()}
        status["captured_at"] = time.monotonic()
        dsp_status.append(status)
        show_status = {key: numeric(value) for key, value in link.request("STATUS").items()}
    finally:
        try:
            link.request("AUDIO OFF")
            link.request("DISARM")
            link.wait_state("OFF", 10)
        finally:
            link.close()
            for controller in controllers:
                controller.close()

    report = {
        "label": args.label,
        "duration_s": args.duration,
        "ports": {"link": link.path, "controllers": [controller.path for controller in controllers]},
        "controllers": identities,
        "summary": summarize(rows, started),
        "show_status": show_status,
        "dsp_status": dsp_status,
        "dsp_diagnostics": link.diagnostics,
        "samples": rows,
    }
    with open(args.output, "w", encoding="utf-8") as output:
        json.dump(report, output, indent=2, sort_keys=True)
    print(json.dumps({key: report[key] for key in ("label", "ports", "controllers", "summary")}, indent=2))


if __name__ == "__main__":
    main()
