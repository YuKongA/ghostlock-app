#!/usr/bin/env python3
"""Convert Sony X-FLASH .sin (CMS container) images to plain .img files.

Sony's current .sin is a "cms" container: a tar-like header, a PKCS#7 CMS
signature block, then the raw partition payload. For boot / init_boot the
payload starts with an `ANDROID!` boot image header (v0-v4); this tool locates
that header, computes the image size from it and writes `<stem>.img`.

Vendor_boot (`VNDRBOOT`) and non-boot payloads are out of scope for now and are
reported as skipped.

Usage:
    python3 tools/sin2img.py <file.sin> [more.sin ...] [--out DIR]
"""
import argparse
import struct
import sys
from pathlib import Path

BOOT_MAGIC = b"ANDROID!"
SCAN_LIMIT = 1 << 20  # the CMS block sits in the first few KB


def align(value: int, page: int) -> int:
    return (value + page - 1) // page * page


def boot_image_size(data: bytes, off: int) -> int:
    """Size of the `ANDROID!` boot image starting at `off`."""
    def u32(field: int) -> int:
        return struct.unpack_from("<I", data, off + field)[0]

    kernel_size = u32(0x08)
    ramdisk_size = u32(0x0C)
    header_version = u32(0x28)
    if header_version >= 3:
        # v3/v4: fixed 4096 alignment, no page_size field.
        page = 4096
        total = page + align(kernel_size, page) + align(ramdisk_size, page)
        if header_version >= 4:
            signature_size = u32(0x62C)
            total += align(signature_size, page) if signature_size else 0
        return total
    # v0-v2: everything is aligned to the page_size field at 0x24.
    page = u32(0x24)
    total = align(page, page)  # header
    total += align(kernel_size, page) + align(ramdisk_size, page)
    total += align(u32(0x18), page)  # second stage
    if header_version >= 1:
        total += align(u32(0x30), page)  # recovery_dtbo
    if header_version >= 2:
        total += align(u32(0x40), page)  # dtb
    return total


def convert(path: Path, out_dir: Path) -> Path | None:
    data = path.read_bytes()
    off = data.find(BOOT_MAGIC, 0, min(len(data), SCAN_LIMIT))
    if off < 0:
        print(f"skip {path.name}: no ANDROID! payload (not a boot/init_boot .sin)")
        return None
    size = boot_image_size(data, off)
    if off + size > len(data):
        raise SystemExit(f"{path.name}: computed image size 0x{size:x} exceeds the file")
    out = out_dir / (path.stem.replace("_X-FLASH-ALL-25B1", "") + ".img")
    out.write_bytes(data[off:off + size])
    print(f"{path.name}: payload@0x{off:x} size={size} -> {out}")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("sins", nargs="+", type=Path)
    ap.add_argument("--out", type=Path, default=None,
                    help="output directory (default: <sin dir>/img)")
    args = ap.parse_args()
    failed = False
    for sin in args.sins:
        out_dir = args.out or (sin.parent / "img")
        out_dir.mkdir(parents=True, exist_ok=True)
        try:
            convert(sin, out_dir)
        except Exception as exc:  # noqa: BLE001 - report per file and continue
            failed = True
            print(f"error {sin.name}: {exc}", file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
