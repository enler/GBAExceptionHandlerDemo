#!/usr/bin/env python3
"""Run the test-only ROM and treat mGBA's 0x0F exit status as success."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: run_smoke.py MGBA_HEADLESS ROM")

    emulator = sys.argv[1]
    rom = Path(sys.argv[2])
    environment = os.environ.copy()
    environment.setdefault("XDG_CONFIG_HOME", "/tmp/gba-exception-demo-mgba")

    completed = subprocess.run(
        [emulator, "-S", "127", "-R", "r0", str(rom)],
        env=environment,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if completed.stdout:
        print(completed.stdout, end="")
    if completed.returncode != 0x0F:
        raise SystemExit(
            "runtime smoke: FAIL: "
            f"mGBA returned 0x{completed.returncode & 0xFF:02X}, expected 0x0F"
        )
    print("runtime smoke: OK: debug | syscall | CLZ | UDF-hook = 0x0F")


if __name__ == "__main__":
    main()
