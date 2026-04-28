#!/usr/bin/env python3

import os
import random
import sys
import time


def exercise_memory_chunk(buffers):
    checksum = 0
    random.shuffle(buffers)
    for buf in buffers:
        for idx in range(0, len(buf), 64):
            value = (buf[idx] + 1) & 0xFF
            buf[idx] = value
            checksum += value
    return checksum


def main() -> int:
    if len(sys.argv) != 3:
        print("Usage: python_memory_workload.py <sleep_seconds> <compute_seconds>", file=sys.stderr)
        return 1

    sleep_seconds = float(sys.argv[1])
    compute_seconds = float(sys.argv[2])

    random.seed(12345)

    print(f"Workload PID: {os.getpid()}", flush=True)
    print(f"Workload sleeping {sleep_seconds:g} seconds before memory computation...", flush=True)
    time.sleep(sleep_seconds)

    print("Workload computation started.", flush=True)
    buffers = [bytearray(2 * 1024 * 1024) for _ in range(32)]
    end_time = time.monotonic() + compute_seconds
    checksum = 0
    while time.monotonic() < end_time:
        checksum += exercise_memory_chunk(buffers)

    print(f"Workload done. checksum={checksum}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
