#!/usr/bin/env python3
"""Flatten a parent function's important call/syscall trace across helpers.

This is the S1 split gate: when run_exploit is decomposed into helpers, the
baseline parent trace must still match the candidate parent + helper traces in
count and order. Argument roots are reported separately because moving code
changes frame/register provenance.
"""
from __future__ import annotations

import argparse
import os
import sys
from typing import Dict, Iterable, List, Sequence, Set, Tuple

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import important_trace as it


def objdump_from_env() -> str:
    path = os.environ.get("LLVM_OBJDUMP")
    if path:
        return path
    root = os.environ.get("ANDROID_NDK_HOME") or os.environ.get("ANDROID_NDK_ROOT")
    if root:
        host = "darwin-x86_64" if sys.platform == "darwin" else "linux-x86_64"
        return os.path.join(root, "toolchains", "llvm", "prebuilt", host, "bin", "llvm-objdump")
    return "llvm-objdump"


def flatten(funcs: Dict[str, Sequence[it.Instr]], name: str,
            helpers: Set[str], symbols: Set[str], stack: Tuple[str, ...]) -> List[dict]:
    if name in stack:
        raise RuntimeError(f"recursive helper: {' -> '.join(stack + (name,))}")
    instrs = funcs.get(name)
    if instrs is None:
        raise RuntimeError(f"function not found: {name}")
    important = it.important_for_function(name, instrs, symbols, 64, False)
    by_addr = {int(item["offset"], 16): item for item in important}
    out: List[dict] = []
    for ins in instrs:
        mnemonic, rest = it.split_mnemonic(ins.text)
        if mnemonic == "bl":
            raw = it.target_symbol(ins.text)
            key = it.symbol_key(raw) if raw else ""
            if key in helpers:
                out.extend(flatten(funcs, key, helpers, symbols, stack + (name,)))
                continue
        item = by_addr.get(ins.addr)
        if item is not None:
            out.append(item)
    return out


def compare(base: Sequence[dict], cur: Sequence[dict],
            verbose: bool = False, ignore: Set[str] | None = None,
            ) -> Tuple[bool, int, List[str]]:
    lines: List[str] = []
    ignore = ignore or set()
    if ignore:
        base = [x for x in base if it.symbol_key(x["target"]) not in ignore]
        cur = [x for x in cur if it.symbol_key(x["target"]) not in ignore]
        lines.append("FLAT-IGNORE " + ",".join(sorted(ignore)))
    arg_lines: List[str] = []
    bt = [it.symbol_key(x["target"]) for x in base]
    ct = [it.symbol_key(x["target"]) for x in cur]
    if len(bt) != len(ct):
        lines.append(f"FLAT-COUNT FAIL base={len(bt)} cur={len(ct)}")
    ok = len(bt) == len(ct)
    for i, (b, c) in enumerate(zip(bt, ct), 1):
        if b != c:
            lines.append(f"FLAT-ORDER FAIL #{i} base={b} cur={c}")
            ok = False
            break
    arg_diff = 0
    for i, (b, c) in enumerate(zip(base, cur), 1):
        for reg in sorted(set(b["args"]) | set(c["args"])):
            if not it.trace_equal(b["args"].get(reg, {}), c["args"].get(reg, {})):
                arg_diff += 1
                arg_lines.append(f"FLAT-ARGS #{i} {b['target']} {reg}")
                break
    if verbose:
        lines.extend(arg_lines)
    lines.append(f"FLAT-SUMMARY base={len(bt)} cur={len(ct)} args_diff={arg_diff}")
    return ok, arg_diff, lines


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("base")
    ap.add_argument("candidate")
    ap.add_argument("--parent", default="run_exploit")
    ap.add_argument("--helper", action="append", default=[])
    ap.add_argument("--objdump", default=objdump_from_env())
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--ignore-symbol", action="append", default=[])
    args = ap.parse_args()

    helpers = set(args.helper)
    symbols = set(it.DEFAULT_SYMBOLS)
    base_funcs = it.disassemble_rich(args.base, args.objdump)
    cur_funcs = it.disassemble_rich(args.candidate, args.objdump)
    base = flatten(base_funcs, args.parent, helpers, symbols, ())
    cur = flatten(cur_funcs, args.parent, helpers, symbols, ())
    ok, arg_diff, lines = compare(base, cur, args.verbose,
                                  {it.symbol_key(x) for x in args.ignore_symbol})
    for line in lines:
        print(line)
    print("FLAT-RESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
