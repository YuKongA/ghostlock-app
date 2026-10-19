#!/usr/bin/env python3
"""Batch-verify ghostlock-extract across a set of kernel images.

Usage:
    python3 tools/verify_kernels.py <boot.img|payload.bin|Image>...

For each input it runs the release extractor (`--format conf`) and prints one
row per image: release, exit code, whether the CVE primitive is present, the
suggested route, the multicast geometry it emitted (waiter_off / task / lock /
compact), the kernel_phys_load source, and any missing sidecar-only field.

No device or root is involved: this is exactly the offline path a bootloader
locked user takes.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "tools", "extract_rs", "target", "release", "ghostlock-extract")


def run(image):
    proc = subprocess.run(
        [BIN, image, "--format", "conf"],
        capture_output=True,
        text=True,
        timeout=1800,
    )
    return proc.returncode, proc.stdout, proc.stderr


def conf_fields(conf):
    fields = {}
    route = None
    stack = []
    for line in conf.splitlines():
        line = line.split("#")[0].strip()
        if not line:
            continue
        if line.endswith("{"):
            stack.append(line[:-1].strip())
            if len(stack) == 2 and stack[0] == "route":
                route = stack[1]
            continue
        if line == "}":
            if stack:
                stack.pop()
            continue
        if "=" in line:
            k, v = (p.strip() for p in line.split("=", 1))
            fields[".".join(stack + [k])] = v
    return route, fields


def classify(err):
    if "already fixed" in err or "rtmutex UAF fix is present" in err:
        return "fixed"
    if "primitive present" in err:
        return "vulnerable"
    return "?"


def waiter_off(err, fields):
    m = re.search(r"waiter_off derived = (0x[0-9a-f]+)", err)
    if m:
        return m.group(1) + " (derived)"
    if "waiter_off derivation failed" in err:
        v = fields.get("route.multicast_waiter.waiter_off")
        return f"{v} (fallback)" if v else "-"
    return fields.get("route.multicast_waiter.waiter_off", "-")


def phys(err, fields):
    if "kernel_phys_load" in fields:
        if "(uefi memory map)" in err:
            return fields["kernel_phys_load"] + " (uefi)"
        if "(xbl_config FDT)" in err:
            return fields["kernel_phys_load"] + " (xbl)"
        return fields["kernel_phys_load"]
    if "5.x" in err or "mtk" in err:
        return "-"
    return "-"


def main():
    if not os.path.isfile(BIN):
        sys.exit(f"missing {BIN}; run: cargo build --release --manifest-path tools/extract_rs/Cargo.toml")
    images = sys.argv[1:]
    if not images:
        sys.exit(__doc__)
    print(f"{'image':<34} {'release':<40} {'st':<4} {'prim':<10} {'route':<17} waiter_off          task/lock  phys            missing")
    for image in images:
        try:
            code, conf, err = run(image)
        except subprocess.TimeoutExpired:
            print(f"{os.path.basename(image):<34} TIMEOUT")
            continue
        route, fields = conf_fields(conf)
        rel = fields.get("release", "-").strip('"')
        prim = classify(err)
        wo = waiter_off(err, fields)
        tl = "-"
        if "route.multicast_waiter.task_offset" in fields:
            tl = f"{fields['route.multicast_waiter.task_offset']}/{fields['route.multicast_waiter.lock_offset']}"
        ph = phys(err, fields)
        missing = "kernel_phys_load" if "kernel_phys_load" not in fields else "-"
        print(
            f"{os.path.basename(image):<34} {rel:<40} {code:<4} {prim:<10} "
            f"{(route or '-'):<17} {wo:<19} {tl:<10} {ph:<15} {missing}"
        )


if __name__ == "__main__":
    main()
