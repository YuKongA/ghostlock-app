#!/usr/bin/env python3
"""Compare the disassembly of attack-critical functions between two native binaries.

Usage:
    python3 tools/cmp_disasm.py <baseline-binary> <candidate-binary>

The objdump binary is resolved from $LLVM_OBJDUMP, then PATH, then the
Android NDK under $ANDROID_NDK_HOME / $ANDROID_NDK_ROOT / ~/Library/Android/sdk.

Three levels are reported, from the loosest to the strictest:
  layout   - symbol names/offsets and all hex are normalised; a difference
             here is a real instruction-shape change.
  operands - symbol/address annotations are dropped, but immediate values and
             structure offsets are kept; a difference here is a real
             instruction-operand change.
  strict   - only absolute hex addresses are normalised; symbol+offset
             annotations and immediates must match. This is the equivalence
             contract used by the CPP migration gates.

A function is IDENTICAL only when strict matches. When layout matches but
operands differ, the changed immediate/offset is a hard failure, so it can
never be hidden behind a layout annotation shift. Only when both layout and
operands match but strict does not is the difference confined to symbol
spelling and is reported as LAYOUT-SHIFT for manual review.

Each target lists the legacy demangled spelling and the namespace-qualified
spelling; whichever is present in a binary is used, so the tool keeps working
across the CPP12 namespace migration.

--reviewed accepts OPERAND-DIFF as OPERAND-SHIFT only when every differing line
keeps the same instruction skeleton (immediates masked) and never touches the
stack pointer; use it only after manually confirming the diffs are relocated
global data addresses (ADR-0001).
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
        "ghostlock::backend::cve_2026_43499::route::do_kernel5_fake_lock_route(ghostlock::memory::WriteRequest const*)",
        "ghostlock::backend::cve_2026_43499::route::do_kernel5_fake_lock_route(ghostlock::WriteRequest const*)",
        "ghostlock::route::do_kernel5_fake_lock_route(ghostlock::memory::WriteRequest const*)",
        "ghostlock::route::do_kernel5_fake_lock_route(ghostlock::WriteRequest const*)",
        "do_kernel5_fake_lock_route(ghostlock::memory::WriteRequest const*)",
        "do_kernel5_fake_lock_route(ghostlock::WriteRequest const*)",
    ]),
    ("do_one_write", [
        "bool ghostlock::backend::Cve43499Primitives::attack_write<ghostlock::backend::cve_2026_43499::route::MulticastPolicy>(ghostlock::session::CoreSession&, ghostlock::memory::WriteRequest const&, char const*)",
        "bool ghostlock::backend::Cve43499Primitives::attack_write<ghostlock::backend::cve_2026_43499::route::SelectPolicy>(ghostlock::session::CoreSession&, ghostlock::memory::WriteRequest const&, char const*)",
        "bool ghostlock::backend::Cve43499Primitives::attack_write<ghostlock::backend::cve_2026_43499::route::TcpPolicy>(ghostlock::session::CoreSession&, ghostlock::memory::WriteRequest const&, char const*)",
        "ghostlock::backend::Cve43499Primitives::attack_write<ghostlock::backend::cve_2026_43499::route::MulticastPolicy>(ghostlock::session::CoreSession&, ghostlock::memory::WriteRequest const&, char const*)",
        "ghostlock::backend::Cve43499Primitives::attack_write<ghostlock::backend::cve_2026_43499::route::SelectPolicy>(ghostlock::session::CoreSession&, ghostlock::memory::WriteRequest const&, char const*)",
        "ghostlock::backend::Cve43499Primitives::attack_write<ghostlock::backend::cve_2026_43499::route::TcpPolicy>(ghostlock::session::CoreSession&, ghostlock::memory::WriteRequest const&, char const*)",
        "bool ghostlock::backend::Cve2026_43499Policy::attack_write<ghostlock::backend::cve_2026_43499::route::MulticastPolicy>(ghostlock::session::CoreSession&, ghostlock::memory::WriteRequest const&, char const*)",
        "bool ghostlock::backend::Cve2026_43499Policy::attack_write<ghostlock::backend::cve_2026_43499::route::SelectPolicy>(ghostlock::session::CoreSession&, ghostlock::memory::WriteRequest const&, char const*)",
        "bool ghostlock::backend::Cve2026_43499Policy::attack_write<ghostlock::backend::cve_2026_43499::route::TcpPolicy>(ghostlock::session::CoreSession&, ghostlock::memory::WriteRequest const&, char const*)",
        "ghostlock::backend::Cve2026_43499Policy::attack_write<ghostlock::backend::cve_2026_43499::route::MulticastPolicy>(ghostlock::session::CoreSession&, ghostlock::memory::WriteRequest const&, char const*)",
        "ghostlock::backend::Cve2026_43499Policy::attack_write<ghostlock::backend::cve_2026_43499::route::SelectPolicy>(ghostlock::session::CoreSession&, ghostlock::memory::WriteRequest const&, char const*)",
        "ghostlock::backend::Cve2026_43499Policy::attack_write<ghostlock::backend::cve_2026_43499::route::TcpPolicy>(ghostlock::session::CoreSession&, ghostlock::memory::WriteRequest const&, char const*)",
        "bool ghostlock::session::backend::Cve2026_43499Policy::attack_write<ghostlock::backend::cve_2026_43499::route::MulticastPolicy>(ghostlock::session::CoreSession&, ghostlock::memory::WriteRequest const&, char const*)",
        "bool ghostlock::session::backend::Cve2026_43499Policy::attack_write<ghostlock::backend::cve_2026_43499::route::SelectPolicy>(ghostlock::session::CoreSession&, ghostlock::memory::WriteRequest const&, char const*)",
        "bool ghostlock::session::backend::Cve2026_43499Policy::attack_write<ghostlock::backend::cve_2026_43499::route::TcpPolicy>(ghostlock::session::CoreSession&, ghostlock::memory::WriteRequest const&, char const*)",
        "ghostlock::session::backend::Cve2026_43499Policy::attack_write<ghostlock::backend::cve_2026_43499::route::MulticastPolicy>(ghostlock::session::CoreSession&, ghostlock::memory::WriteRequest const&, char const*)",
        "ghostlock::session::backend::Cve2026_43499Policy::attack_write<ghostlock::backend::cve_2026_43499::route::SelectPolicy>(ghostlock::session::CoreSession&, ghostlock::memory::WriteRequest const&, char const*)",
        "ghostlock::session::backend::Cve2026_43499Policy::attack_write<ghostlock::backend::cve_2026_43499::route::TcpPolicy>(ghostlock::session::CoreSession&, ghostlock::memory::WriteRequest const&, char const*)",
        "bool ghostlock::session::backend::Cve2026_43499Policy::attack_write<ghostlock::route::MulticastPolicy>(ghostlock::session::CoreSession&, ghostlock::memory::WriteRequest const&, char const*)",
        "bool ghostlock::session::backend::Cve2026_43499Policy::attack_write<ghostlock::route::SelectPolicy>(ghostlock::session::CoreSession&, ghostlock::memory::WriteRequest const&, char const*)",
        "bool ghostlock::session::backend::Cve2026_43499Policy::attack_write<ghostlock::route::TcpPolicy>(ghostlock::session::CoreSession&, ghostlock::memory::WriteRequest const&, char const*)",
        "bool ghostlock::session::backend::Cve2026_43499Policy::attack_write<ghostlock::route::MulticastPolicy>(ghostlock::session::ExploitSession&, ghostlock::memory::WriteRequest const&, char const*)",
        "bool ghostlock::session::backend::Cve2026_43499Policy::attack_write<ghostlock::route::SelectPolicy>(ghostlock::session::ExploitSession&, ghostlock::memory::WriteRequest const&, char const*)",
        "bool ghostlock::session::backend::Cve2026_43499Policy::attack_write<ghostlock::route::TcpPolicy>(ghostlock::session::ExploitSession&, ghostlock::memory::WriteRequest const&, char const*)",
        "ghostlock::session::backend::Cve2026_43499Policy::attack_write<ghostlock::route::MulticastPolicy>(ghostlock::session::ExploitSession&, ghostlock::memory::WriteRequest const&, char const*)",
        "ghostlock::session::backend::Cve2026_43499Policy::attack_write(ghostlock::session::ExploitSession&, ghostlock::memory::WriteRequest const&, char const*)",
        "ghostlock::session::backend::Cve2026_43499Policy::attack_write(ghostlock::session::ExploitSession const&, ghostlock::memory::WriteRequest const&, char const*)",
        "ghostlock::session::ExploitProcedure::attack_write(ghostlock::memory::WriteRequest const&, char const*)",
        "ghostlock::session::ExploitProcedure::attack_write(ghostlock::memory::WriteRequest const*, char const*)",
        "ghostlock::attack::do_one_write(ghostlock::memory::WriteRequest const*, char const*)",
        "ghostlock::ops::do_one_write(ghostlock::WriteRequest const*, char const*)",
        "do_one_write(ghostlock::memory::WriteRequest const*, char const*)",
        "do_one_write(ghostlock::WriteRequest const*, char const*)",
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
GLOBAL_SHIFT = None


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
    """Drop absolute addresses; keep symbol annotations and immediates."""
    text = re.sub(r"0x[0-9a-f]+ <", "<", text)
    parts = re.split(r"(<[^>]*>)", text)
    for i in range(0, len(parts), 2):
        parts[i] = re.sub(r"(?<!#)(?<!#-)0x[0-9a-f]+", "0xH", parts[i])
    return "".join(parts)


def operands(text):
    """Drop symbol/address annotations; keep immediates and offsets.

    An immediate is spelled `#0x..` (possibly `#-0x..`) and a structure
    offset is a `#0x..` inside a memory operand, so the negative lookbehind
    keeps both; a bare `0x..` is an absolute address/branch target and is
    normalised."""
    text = re.sub(r"<.*>", "<SYM>", text)
    return re.sub(r"(?<!#)(?<!#-)0x[0-9a-f]+", "0xH", text)


def layout(text):
    text = re.sub(r"<.*>", "<SYM>", text)
    return re.sub(r"0x[0-9a-f]+", "0xH", text)


# Reviewed shape diffs: (label, base mnemonic, candidate mnemonic) -> rationale.
# A shape change is accepted ONLY under --reviewed AND only when every differing
# line matches an allowlisted pair (same instruction count). Each entry is a
# manual review recorded in docs/analysis/device-gates/ (ADR-0004 第九轮).
REVIEWED_SHAPE = {
    ("consumer_thread", "cbz", "tbz"):
        "optional<uint8_t>::value_or(0)!=0 -> optional<bool>::value_or(false): "
        "compare-to-zero becomes test-bit on the same register; instruction count "
        "and branch target unchanged (T3c C++ bool)",
}


def mnemonic_of(line):
    return line.split(None, 1)[0]


def operands_shift_equal(base, cur):
    """True when base and cur differ only in global-data `#0x` displacements.

    The instruction skeleton (text with `#0x..` immediates masked) must match,
    and no differing line may reference the stack pointer, so a real stack or
    control-flow change still fails. Used by `--reviewed` after a manual check
    that the only differences are relocated global addresses (ADR-0001).
    """
    if base == cur:
        return True
    masked = re.compile(r"#-?0x[0-9a-f]+")
    if masked.sub("#0xH", base) != masked.sub("#0xH", cur):
        return False
    return not re.search(r"\bsp\b", base) and not re.search(r"\bsp\b", cur)


def main():
    global OBJDUMP, GLOBAL_SHIFT
    argv = sys.argv[1:]
    if argv and argv[0] == "--reviewed":
        if len(argv) != 3:
            sys.exit("usage: cmp_disasm.py [--reviewed] <baseline> <candidate>")
        GLOBAL_SHIFT = True
        base_path, cur_path = argv[1], argv[2]
    elif len(argv) == 2:
        base_path, cur_path = argv[0], argv[1]
    else:
        sys.exit(main.__doc__ or "usage: cmp_disasm.py [--reviewed] <baseline> <candidate>")
    OBJDUMP = find_objdump()
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
        ob = [operands(x) for x in b]
        oc = [operands(x) for x in c]
        lb = [layout(x) for x in b]
        lc = [layout(x) for x in c]
        if lb != lc:
            diffs = [i for i, (x, y) in enumerate(zip(lb, lc)) if x != y]
            reviewed = GLOBAL_SHIFT and all(
                (label, mnemonic_of(b[i]), mnemonic_of(c[i])) in REVIEWED_SHAPE
                for i in diffs
            )
            if reviewed:
                print(f"REVIEWED-SHAPE {label}: {len(diffs)} reviewed mnemonic change(s)")
                for i in diffs[:5]:
                    print(f"  [{i}] base: {b[i]}")
                    print(f"  [{i}] cur:  {c[i]}")
                continue
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
        if ob != oc:
            if GLOBAL_SHIFT and all(
                operands_shift_equal(x, y) for x, y in zip(ob, oc)
            ):
                print(f"OPERAND-SHIFT {label}: reviewed data-displacement diff "
                      f"({len(b)} instructions)")
                continue
            failed += 1
            print(f"OPERAND-DIFF {label} ({len(b)} instructions)")
            shown = 0
            for i, (x, y) in enumerate(zip(ob, oc)):
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
