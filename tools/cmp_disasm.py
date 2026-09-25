#!/usr/bin/env python3
"""Compare the disassembly of attack-critical functions between two native binaries.

Usage:
    python3 tools/cmp_disasm.py <baseline-binary> <candidate-binary>

The objdump binary is resolved from $LLVM_OBJDUMP, then PATH, then the
Android NDK under $ANDROID_NDK_HOME / $ANDROID_NDK_ROOT / ~/Library/Android/sdk.

Two levels are reported:
  strict  - only absolute hex addresses are normalised; symbol+offset
            annotations must match. This is the equivalence contract used by
            the CPP migration gates.
  layout  - symbol names/offsets are normalised too; any remaining difference
            is a real instruction-shape change.

Each target lists the legacy demangled spelling and the namespace-qualified
spelling; whichever is present in a binary is used, so the tool keeps working
across the CPP12 namespace migration.
"""
import glob
import os
import re
import shutil
import subprocess
import sys

TARGETS = [
    ("owner_thread", [
        "ghostlock::race::owner_thread(void*)",
        "owner_thread(void*)",
    ]),
    ("waiter_thread", [
        "ghostlock::race::waiter_thread(void*)",
        "waiter_thread(void*)",
    ]),
    ("consumer_thread", [
        "ghostlock::race::consumer_thread(void*)",
        "consumer_thread(void*)",
    ]),
    ("run_main_route_threads", [
        "ghostlock::race::run_main_route_threads(ghostlock::memory::WriteRequest const&)",
        "ghostlock::race::run_main_route_threads(ghostlock::memory::WriteRequest const*)",
        "ghostlock::race::run_main_route_threads(ghostlock::WriteRequest const*)",
        "run_main_route_threads(ghostlock::memory::WriteRequest const&)",
        "run_main_route_threads(ghostlock::memory::WriteRequest const*)",
        "run_main_route_threads(ghostlock::WriteRequest const*)",
    ]),
    ("do_kernel5_fake_lock_route", [
        "ghostlock::route::do_kernel5_fake_lock_route(ghostlock::memory::WriteRequest const*)",
        "ghostlock::route::do_kernel5_fake_lock_route(ghostlock::WriteRequest const*)",
        "do_kernel5_fake_lock_route(ghostlock::memory::WriteRequest const*)",
        "do_kernel5_fake_lock_route(ghostlock::WriteRequest const*)",
    ]),
    ("do_one_write", [
        "ghostlock::session::backend::Cve2026_43499Policy::attack_write(ghostlock::session::ExploitSession&, ghostlock::memory::WriteRequest const&, char const*)",
        "ghostlock::session::backend::Cve2026_43499Policy::attack_write(ghostlock::session::ExploitSession const&, ghostlock::memory::WriteRequest const&, char const*)",
        "ghostlock::session::ExploitProcedure::attack_write(ghostlock::memory::WriteRequest const&, char const*)",
        "ghostlock::session::ExploitProcedure::attack_write(ghostlock::memory::WriteRequest const*, char const*)",
        "ghostlock::attack::do_one_write(ghostlock::memory::WriteRequest const*, char const*)",
        "ghostlock::ops::do_one_write(ghostlock::WriteRequest const*, char const*)",
        "do_one_write(ghostlock::memory::WriteRequest const*, char const*)",
        "do_one_write(ghostlock::WriteRequest const*, char const*)",
    ]),
    ("multicast_owner_worker", [
        "ghostlock::route::multicast_waiter::(anonymous namespace)::multicast_owner_worker(void*)",
        "ghostlock::route::multicast_owner_worker(void*)",
        "(anonymous namespace)::multicast_owner_worker(void*)",
        "multicast_owner_worker(void*)",
    ]),
    ("multicast_waiter_worker", [
        "ghostlock::route::multicast_waiter::(anonymous namespace)::multicast_waiter_worker(void*)",
        "ghostlock::route::multicast_waiter_worker(void*)",
        "(anonymous namespace)::multicast_waiter_worker(void*)",
        "multicast_waiter_worker(void*)",
    ]),
]


def find_objdump():
    env = os.environ.get("LLVM_OBJDUMP")
    if env:
        return env
    found = shutil.which("llvm-objdump")
    if found:
        return found
    roots = [os.environ.get("ANDROID_NDK_HOME"),
             os.environ.get("ANDROID_NDK_ROOT"),
             os.path.expanduser("~/Library/Android/sdk/ndk"),
             os.path.expanduser("~/Android/Sdk/ndk")]
    for root in (r for r in roots if r):
        pattern = os.path.join(root, "*", "toolchains", "llvm", "prebuilt",
                               "*", "bin", "llvm-objdump")
        hits = sorted(glob.glob(pattern))
        if hits:
            return hits[-1]
    sys.exit("llvm-objdump not found; set LLVM_OBJDUMP or ANDROID_NDK_HOME")


OBJDUMP = None


def disassemble(path):
    out = subprocess.run([OBJDUMP, "-d", "--no-show-raw-insn", "--demangle", path],
                         capture_output=True, text=True, check=True).stdout
    funcs = {}
    current = None
    header = re.compile(r"^([0-9a-f]+) <(.+)>:$")
    for line in out.splitlines():
        m = header.match(line)
        if m:
            current = m.group(2)
            funcs.setdefault(current, [])
            continue
        if current is None:
            continue
        text = line.strip()
        if not text:
            continue
        text = re.sub(r"^[0-9a-f]+:\s*", "", text)
        funcs[current].append(text)
    return funcs


def resolve(funcs, candidates):
    for name in candidates:
        if name in funcs:
            return name
    return None


def strict(text):
    text = re.sub(r"0x[0-9a-f]+ <", "<", text)
    return re.sub(r"0x[0-9a-f]+", "0xH", text)


def layout(text):
    text = re.sub(r"<.*>", "<SYM>", text)
    return re.sub(r"0x[0-9a-f]+", "0xH", text)


def main():
    global OBJDUMP
    if len(sys.argv) != 3:
        sys.exit(main.__doc__ or "usage: cmp_disasm.py <baseline> <candidate>")
    OBJDUMP = find_objdump()
    base_path, cur_path = sys.argv[1], sys.argv[2]
    base, cur = disassemble(base_path), disassemble(cur_path)
    failed = 0
    for label, candidates in TARGETS:
        base_name = resolve(base, candidates)
        cur_name = resolve(cur, candidates)
        if base_name is None or cur_name is None:
            print(f"MISSING {label}: base={base_name is not None} cur={cur_name is not None}")
            failed += 1
            continue
        b, c = base[base_name], cur[cur_name]
        if len(b) != len(c):
            print(f"DIFF {label}: instruction count base={len(b)} cur={len(c)}")
            failed += 1
            continue
        sb = [strict(x) for x in b]
        sc = [strict(x) for x in c]
        lb = [layout(x) for x in b]
        lc = [layout(x) for x in c]
        if lb != lc:
            failed += 1
            print(f"SHAPE-DIFF {label} ({len(b)} instructions)")
            shown = 0
            for i, (x, y) in enumerate(zip(lb, lc)):
                if x != y:
                    print(f"  [{i}] base: {b[i]}")
                    print(f"  [{i}] cur: {c[i]}")
                    shown += 1
                    if shown >= 5:
                        break
            continue
        if sb == sc:
            print(f"IDENTICAL {label} ({len(b)} instructions, strict)")
        else:
            diff = sum(1 for x, y in zip(sb, sc) if x != y)
            print(f"LAYOUT-SHIFT {label}: {len(b)} instructions, "
                  f"{diff} annotated address operands differ")
    print("RESULT:", "FAIL" if failed else "PASS")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
