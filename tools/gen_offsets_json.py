#!/usr/bin/env python3
"""regenerate the aggregate offsets.json from src/kernels/*/offsets.h.

run locally after adding a kernel header, and in ci on every push touching
src/kernels/.  the app fetches offsets.json at startup, so this keeps the
published table current without an app rebuild.
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

SYMBOLS = [
    "off_init_task", "off_init_cred", "off_root_task_group", "off_selinux_enforcing",
    "off_selinux_blob_sizes", "off_security_hook_heads", "off_slide_nfulnl_logger",
    "off_slide_loggers_0_1", "off_slide_boot_id",
]
STRUCT = [
    "task_prio", "task_normal_prio", "task_sched_task_group",
    "task_pi_lock", "task_pi_waiters", "task_pi_top_task", "task_pi_blocked_on",
    "task_pid", "task_tgid", "task_atomic_flags",
    "task_real_cred", "task_cred", "task_comm", "task_tasks", "task_seccomp",
]


def parse_val(v):
    v = v.strip()
    if v.startswith("0x") or v.startswith("0X"):
        return int(v, 16)
    return int(v)


def read_macros():
    text = (ROOT / "src/kernels/offsets.h").read_text(encoding="utf-8")
    macros = {}
    for name in ("STRUCT_OFFSETS_6_6", "STRUCT_OFFSETS_6_12"):
        m = re.search(r"#define\s+" + name + r"\s+\\\n(.*?)(?=\n\S|$)", text, re.S)
        if not m:
            sys.exit(f"macro {name} not found")
        body = m.group(1).replace("\\\n", " ")
        fields = {}
        for fm in re.finditer(r"\.(\w+)\s*=\s*(-?0x[0-9a-fA-F]+|-?\d+)", body):
            fields[fm.group(1)] = parse_val(fm.group(2))
        macros[name] = fields
    return macros


def parse_kernel(path, macros):
    text = path.read_text(encoding="utf-8")
    fields = {}
    m = re.search(r'"(6\.\d+[^"]*)"', text)
    if not m:
        sys.exit(f"no release in {path}")
    release = m.group(1)
    mm = re.search(r"STRUCT_OFFSETS_6_(\d+)", text)
    if mm:
        fields.update(macros[f"STRUCT_OFFSETS_6_{mm.group(1)}"])
    for fm in re.finditer(r"\.(\w+)\s*=\s*(-?0x[0-9a-fA-F]+|-?\d+)", text):
        fields[fm.group(1)] = parse_val(fm.group(2))
    entry = {"release": release}
    if "kernel_phys_load" in fields:
        entry["kernel_phys_load"] = fields["kernel_phys_load"]
    if "pselect_waiter_shift" in fields:
        entry["pselect_waiter_shift"] = fields["pselect_waiter_shift"]
    sym = {k: fields[k] for k in SYMBOLS if k in fields}
    if sym:
        entry["symbols"] = sym
    st = {k: fields[k] for k in STRUCT if k in fields}
    if st:
        entry["struct_fields"] = st
    return entry


def main():
    macros = read_macros()
    # Preserve src/kernels/offsets.h include order.
    order = []
    for line in (ROOT / "src/kernels/offsets.h").read_text(encoding="utf-8").splitlines():
        m = re.search(r'include "([^"]+)"', line)
        if m:
            order.append(m.group(1))
    entries = [parse_kernel(ROOT / "src/kernels" / rel, macros) for rel in order]
    out = ROOT / "offsets.json"
    with out.open("w", encoding="utf-8", newline="\n") as f:
        json.dump(entries, f, indent=2)
        f.write("\n")
    print(f"wrote {out}: {len(entries)} entries")


main()
