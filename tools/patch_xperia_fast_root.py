#!/usr/bin/env python3
"""Remove the two-second post-W2 delay from the known-good Xperia V3 ELF."""

import hashlib
import sys
from pathlib import Path

SOURCE_SHA256 = "91c2fcb49edd13bfd8bafbee00a9c20e7c97a688305b284c677ff12c6740a89c"
OUTPUT_SHA256 = "83856c50e7f65a0360bf9e30e1f9ac6900fa4d66b499da762c7308924192e6bf"
FILE_OFFSET = 0xA4E4
SLEEP_CALL = bytes.fromhex("17130094")
AARCH64_NOP = bytes.fromhex("1f2003d5")


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} INPUT OUTPUT", file=sys.stderr)
        return 2
    data = bytearray(Path(sys.argv[1]).read_bytes())
    if digest(data) != SOURCE_SHA256:
        raise SystemExit("input is not the known Xperia V3 binary")
    if data[FILE_OFFSET : FILE_OFFSET + 4] != SLEEP_CALL:
        raise SystemExit("post-W2 sleep instruction does not match")
    data[FILE_OFFSET : FILE_OFFSET + 4] = AARCH64_NOP
    if digest(data) != OUTPUT_SHA256:
        raise SystemExit("patched binary hash mismatch")
    Path(sys.argv[2]).write_bytes(data)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
