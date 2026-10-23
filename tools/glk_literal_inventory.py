#!/usr/bin/env python3
"""Generate a numeric-literal inventory for the 43499/43284 C payloads.

Usage:
    python3 tools/glk_literal_inventory.py [--root app/src/main/cpp] [--out build/analysis/literals.tsv]

The output is a TSV with columns:
    file, line, value, class, context

class is a *tentative* A-E bucket from the parameterization plan:
    A target/layout, B config/tunable, C runtime/sentinel,
    E ABI/algorithm/protocol, L local 0/1, ? needs manual review.

Comments and string/char literals are masked before matching, so paths and log
text do not pollute the inventory. Macros are classified from their defining
line; function-body numbers default to ? unless a known syscall/flag prefix or
a target header makes them unambiguous. The '?' rows are the manual work list.
"""
from __future__ import annotations

import argparse
import os
import re
from pathlib import Path

SUFFIXES = {".c", ".h", ".cpp", ".hpp", ".cc", ".S", ".s"}
NUM_RE = re.compile(r"(?<![\w.])(0[xX][0-9a-fA-F]+|[0-9]+(?:\.[0-9]+)?)(?:[uUlL]+)?\b")
ABI_HINTS = (
    "SYS_", "__NR_", "ELF", "AES", "O_", "CLONE_", "MAP_", "PROT_",
    "SPLICE_", "FALLOC_", "MSG_", "SO_", "IPPROTO_", "TCP_", "FUTEX_",
    "PERF_", "PR_", "SIG", "SEEK_", "AT_", "MFD_", "BLKROSET", "WIF",
    "WEXIT", "SIGCHLD", "STDIN", "STDOUT", "STDERR", "AF_", "SOCK_",
)
ENV_HINTS = ("getenv", "direct_map", "selinux", "seccomp", "kernelsu",
             "zygote", "uname", "__system_property")
RUNTIME_HINTS = ("atomic_", "futex", "waiter_", "owner_", "consumer_",
                 "mr_", "tcp_punch", "page_base", "fake_", "reclaim",
                 "prebuilt", "pselect_custom", "g_seq", "route_last")
CONFIG_HINTS = ("usleep", "sleep(", "timeout", "deadline", "settle",
                "attempt", "spin", "wait", "budget", "char ", "sndbuf",
                "F_DUPFD", "PSELECT_ROUTE_NFDS")

MANUAL = {
    ("exp.c", 412): ("E", "AArch64 B/BL branch encoding"),
    ("exp.c", 430): ("E", "AArch64 branch immediate mask"),
    ("exp.c", 436): ("E", "AArch64 branch immediate mask"),
    ("fops.c", 8): ("E", "ms unit conversion"),
    ("fops.c", 64): ("E", "futex PI marker"),
    ("fops.c", 380): ("B", "progress log interval"),
    ("fops.c", 665): ("E", "us to ns conversion"),
    ("main.c", 148): ("A", "minimum credential copy size"),
    ("main.c", 253): ("E", "ms unit conversion"),
    ("main.c", 310): ("E", "futex PI marker"),
    ("main.c", 367): ("B", "consumer nice cycle"),
    ("main.c", 371): ("B", "futex timeout ns"),
    ("main.c", 513): ("B", "slab drain age threshold"),
    ("main.c", 514): ("B", "slab drain waves/batch"),
    ("main.c", 547): ("E", "strtol base 10"),
    ("main.c", 556): ("E", "strtol base 10"),
    ("main.c", 756): ("E", "script file mode"),
    ("main.c", 783): ("B", "perf sample period"),
    ("main.c", 785): ("B", "perf register width"),
    ("main.c", 797): ("B", "perf buffer pages"),
    ("main.c", 811): ("B", "perf data pages"),
    ("main.c", 813): ("B", "perf candidate capacity"),
    ("main.c", 814): ("B", "perf candidate capacity"),
    ("main.c", 823): ("B", "perf register count"),
    ("main.c", 826): ("E", "tag nibble/canonical shift"),
    ("main.c", 886): ("E", "probe sentinel"),
    ("main.c", 905): ("E", "probe sentinel"),
    ("main.c", 945): ("B", "fd sweep cap"),
    ("main.c", 1071): ("C", "uid sentinel"),
    ("main.c", 1095): ("E", "probe sentinel"),
    ("main.c", 1096): ("E", "ENOSYS"),
    ("main.c", 1112): ("E", "report field mask"),
    ("main.c", 1125): ("E", "comm length"),
    ("main.c", 1213): ("A", "2 MiB alignment mask"),
    ("main.c", 1338): ("B", "W2 attempts/settle"),
    ("main.c", 1370): ("B", "W3 attempts/settle"),
    ("main.c", 1460): ("B", "KSU log poll count"),
    ("main.c", 1479): ("B", "enforce poll count"),
    ("util.c", 23): ("E", "ms unit conversion"),
    ("util.c", 24): ("E", "us unit conversion"),
    ("util.c", 180): ("E", "sched_setattr syscall number"),
    ("util.c", 430): ("A", "skb payload delta"),
    ("util.c", 484): ("A", "W0 tree entry offset"),
    ("util.c", 485): ("A", "W0 tree entry offset"),
    ("util.c", 487): ("A", "W0 tree entry offset"),
    ("util.c", 488): ("A", "W0 tree entry offset"),
    ("util.c", 491): ("A", "W0 pi tree entry offset"),
    ("util.c", 492): ("A", "W0 pi tree entry offset"),
    ("util.c", 493): ("A", "W0 pi tree entry offset"),
    ("util.c", 497): ("A", "W0 wake_state offset"),
    ("util.c", 498): ("A", "W0 prio offset"),
    ("util.c", 500): ("A", "W0 ww_ctx offset"),
    ("util.c", 504): ("A", "W0 tree entry offset"),
    ("util.c", 505): ("A", "W0 tree entry offset"),
    ("util.c", 509): ("A", "W0 pi tree entry offset"),
    ("util.c", 510): ("A", "W0 pi tree entry offset"),
    ("util.c", 547): ("A", "RIGHT payload offset"),
    ("util.c", 548): ("A", "RIGHT payload offset"),
    ("util.c", 551): ("A", "LEFT payload offset"),
    ("util.c", 552): ("A", "LEFT payload offset"),
    ("util.c", 574): ("E", "skb fill byte"),
    ("util.c", 645): ("E", "scan id shift/mask"),
    ("util.c", 650): ("E", "percentage conversion"),
    ("util.c", 679): ("E", "tag nibble/canonical shift"),
}



def mask_code(text: str) -> str:
    """Replace comments and string/char literal bodies with spaces."""
    out = []
    i = 0
    n = len(text)
    state = "code"
    while i < n:
        ch = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if state == "code":
            if ch == "/" and nxt == "/":
                out.append("  ")
                i += 2
                state = "line"
                continue
            if ch == "/" and nxt == "*":
                out.append("  ")
                i += 2
                state = "block"
                continue
            if ch == '"':
                out.append(" ")
                i += 1
                state = "string"
                continue
            if ch == "'":
                out.append(" ")
                i += 1
                state = "char"
                continue
            out.append(ch)
            i += 1
            continue
        if state == "line":
            if ch == "\n":
                out.append("\n")
                state = "code"
            else:
                out.append(" ")
            i += 1
            continue
        if state == "block":
            if ch == "*" and nxt == "/":
                out.append("  ")
                i += 2
                state = "code"
                continue
            out.append("\n" if ch == "\n" else " ")
            i += 1
            continue
        if state in ("string", "char"):
            if ch == "\\":
                out.append("  ")
                i += 2
                continue
            if (state == "string" and ch == '"') or (state == "char" and ch == "'"):
                out.append(" ")
                i += 1
                state = "code"
                continue
            out.append("\n" if ch == "\n" else " ")
            i += 1
            continue
    return "".join(out)


def classify(path: Path, line: str, value: str, in_define: bool, line_no: int) -> tuple[str, str]:
    name = path.name
    manual = MANUAL.get((name, line_no))
    if manual is not None:
        return manual
    if name == "aes256.h":
        return ("E", "AES algorithm constant")
    if name in {"hmac_sha256.cpp", "hmac_sha256.hpp"}:
        return ("E", "HMAC algorithm constant")
    if name in {"kernelsnitch.h", "utils.h", "futex_hash.h", "timeutils.h"} or "kernelsnitch" in path.parts:
        return ("E", "KernelSnitch protocol constant")
    if name == "elf_parser.c":
        return ("E", "ELF ABI constant")
    if name == "offsets_json.c":
        if "char " in line:
            return ("B", "JSON parser buffer size")
        return ("E", "JSON parser protocol constant")
    if name == "gl_log.c":
        if "char " in line:
            return ("B", "log buffer size")
        return ("E", "logging ABI/unit constant")
    header = name in {"target.h", "common.h", "runtime_struct_offsets.h", "offsets.h"} or "kernels" in path.parts
    if in_define:
        if header:
            return ("A", "target/layout macro")
        return ("E", "protocol/ABI macro")
    if any(h in line for h in ABI_HINTS):
        return ("E", "ABI/protocol name")
    if any(h in line for h in ENV_HINTS):
        return ("D", "environment probe")
    if any(h in line for h in RUNTIME_HINTS):
        return ("C", "runtime state")
    if any(h in line for h in CONFIG_HINTS):
        return ("B", "tunable/timing")
    if any(w in line for w in ('"tree_', '"pi_', '"task"', '"lock"',
                               '"wake_', '"deadline"', '"ww_ctx"')):
        return ("A", "waiter word layout")
    if "zc" in line or "0x55" in line or "0x33" in line:
        return ("E", "wire fill/offset")
    if header:
        return ("A", "target/layout header")
    dec = value.lower().startswith("0x")
    ival = int(value, 16) if dec else int(float(value))
    if ival in (0, 1):
        return ("L", "local 0/1")
    if path.suffix == ".S":
        return ("E", "assembly ABI")
    if name in {"exp.c", "bootstrap.c", "splicehelper.c"} and ival < 100000:
        return ("B", "config/tunable")
    return ("?", "needs manual review")


def iter_files(root: Path):
    for path in sorted(root.rglob("*")):
        if path.is_file() and path.suffix in SUFFIXES and "kernels" not in path.parts:
            yield path


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="app/src/main/cpp")
    ap.add_argument("--out", default="build/analysis/literals.tsv")
    args = ap.parse_args()
    root = Path(args.root)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    rows = []
    for path in iter_files(root):
        text = path.read_text(errors="replace")
        masked = mask_code(text)
        raw_lines = text.splitlines()
        masked_lines = masked.splitlines()
        for idx, mline in enumerate(masked_lines, 1):
            original = raw_lines[idx - 1] if idx <= len(raw_lines) else ""
            in_define = original.lstrip().startswith("#define")
            for match in NUM_RE.finditer(mline):
                value = match.group(1)
                # Ignore array indices and tiny local constants in code; keep 0/1
                # only when they appear in a macro or a named flag line.
                if len(value) < 2 and not value.startswith("0x") and value not in ("0", "1"):
                    continue
                cls, reason = classify(path, original, value, in_define, idx)
                source = "manual" if (path.name, idx) in MANUAL else "auto"
                context = original.strip().replace("\t", " ")
                if len(context) > 120:
                    context = context[:117] + "..."
                rows.append((str(path), idx, value, cls, source, reason, context))
    with out.open("w") as f:
        f.write("file\tline\tvalue\tclass\tsource\treason\tcontext\n")
        for row in rows:
            f.write("\t".join(str(x) for x in row) + "\n")
    counts = {}
    for row in rows:
        counts[row[3]] = counts.get(row[3], 0) + 1
    print(f"wrote {out}: {len(rows)} literals")
    for key in sorted(counts):
        print(f"  {key}: {counts[key]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
