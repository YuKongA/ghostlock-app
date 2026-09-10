#!/usr/bin/env python3
"""Remove the two-second post-W2 delay from the known-good Xperia V3 ELF."""

import hashlib
import sys
from pathlib import Path

SOURCE_SHA256 = "91c2fcb49edd13bfd8bafbee00a9c20e7c97a688305b284c677ff12c6740a89c"
OUTPUT_SHA256 = "83856c50e7f65a0360bf9e30e1f9ac6900fa4d66b499da762c7308924192e6bf"
BOUNDED_OUTPUT_SHA256 = "997b1c9f0c8eb020afc2f6b89d2aab15e40311ce43d1d4257e6377c90fc5fce2"
SLEEP_FILE_OFFSET = 0xA4E4
W1_ATTEMPTS_FILE_OFFSET = 0x9C50
SLEEP_CALL = bytes.fromhex("17130094")
AARCH64_NOP = bytes.fromhex("1f2003d5")
W1_ATTEMPTS_15 = bytes.fromhex("e3018052")
W1_ATTEMPTS_2 = bytes.fromhex("43008052")


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    if len(sys.argv) not in (3, 4):
        print(f"usage: {sys.argv[0]} INPUT OUTPUT [--bounded-w1]", file=sys.stderr)
        return 2
    data = bytearray(Path(sys.argv[1]).read_bytes())
    if digest(data) != SOURCE_SHA256:
        raise SystemExit("input is not the known Xperia V3 binary")
    if data[SLEEP_FILE_OFFSET : SLEEP_FILE_OFFSET + 4] != SLEEP_CALL:
        raise SystemExit("post-W2 sleep instruction does not match")
    data[SLEEP_FILE_OFFSET : SLEEP_FILE_OFFSET + 4] = AARCH64_NOP
    expected = OUTPUT_SHA256
    if len(sys.argv) == 4:
        if sys.argv[3] != "--bounded-w1":
            raise SystemExit("unknown option")
        if data[W1_ATTEMPTS_FILE_OFFSET : W1_ATTEMPTS_FILE_OFFSET + 4] != W1_ATTEMPTS_15:
            raise SystemExit("W1 attempt instruction does not match")
        data[W1_ATTEMPTS_FILE_OFFSET : W1_ATTEMPTS_FILE_OFFSET + 4] = W1_ATTEMPTS_2
        expected = BOUNDED_OUTPUT_SHA256
    if digest(data) != expected:
        raise SystemExit("patched binary hash mismatch")
    Path(sys.argv[2]).write_bytes(data)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
