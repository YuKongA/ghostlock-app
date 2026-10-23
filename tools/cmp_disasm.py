#!/usr/bin/env python3
"""Compare attack-critical disassembly and trace important call/syscall arguments.

功能总览
========
1. 函数级对比（保留原有行为）
   - strict / layout / operands 三档；--reviewed 归一化可解释的 ELF 重定位；
   - --partial 跳过一个二进制中不存在的目标；
   - --count-delta N 允许受控的指令数变化，但要求 baseline 的 bl 顺序仍是
     candidate 的子序列。
2. 重要调用 / syscall 筛选（--important / --important-only）
   - 默认筛选 futex/socket/mmap/ioctl/open/read/write/clone/execve/
     waitpid/pthread_*/syscall 等攻击相关符号；
   - svc #0 始终记录；blr 由 --trace-indirect 开启；
   - 记录每个函数的重要指令数量、顺序、偏移、kind、target、参数寄存器。
3. 参数链追踪与验证
   - 从重要指令前一条反向扫描，追踪每个参数寄存器到根：
     arg / fixed / frame / frame-load / load / computed / call-return /
     syscall-return / system-register / unknown；
   - 记录传入前每一次写该寄存器的指令（offset + text）；
   - baseline vs candidate 比较数量、顺序、根类型、根值和完整修改链；
   - 归一化 adrp 地址链、栈帧偏移、参数个数，避免布局变化造成假差异。
4. 输出
   - 人类可读 IMPORTANT-SUMMARY / COUNT / ORDER / TRACE / RESULT；
   - --important-json 输出双份结构化分析；--important-verbose 打印参数树。

Usage:
    python3 tools/cmp_disasm.py [--reviewed] [--partial] <baseline-binary> <candidate-binary>

--partial skips a target when it is absent from BOTH binaries (used when one
binary contains only one backend). A target present in one binary but not the
other is still a failure.

--count-delta N accepts an instruction-count change up to N only when every
baseline `bl` target still appears in the candidate in the same order; a
removed or reordered call still fails. It is an interface-change diagnostic,
not a replacement for strict/reviewed equivalence.

--important runs the important-call/syscall extractor after the existing
comparison (or alone with --important-only). It records call/syscall count,
order, and each argument chain from its root to the call. --important-verbose prints the trees; --important-json writes both analyses
as JSON; --important-symbols FILE adds call names; --trace-depth N limits
recursion; --trace-indirect also follows blr; --important-ignore-new and
--important-ignore-removed treat one-sided functions as informational.

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

Relocation normalisation (--reviewed only)
------------------------------------------
AArch64 reaches read-only data with a PC-relative `adrp Xn, #page` followed by
a memory operand `[Xn, #off]`; the address the CPU dereferences is the
effective address `page + off`. When .rodata grows or shrinks the linker can
move one immutable object and re-partition that object's address between the
`adrp` page and the `ldr/str` displacement, so the *same* object is spelled
`adrp x9, #0xb000; ... ldr d1, [x9, #0x698]` in one build and
`adrp x9, #0xc000; ... ldr d1, [x9]` in the next. The immediates alone look
like a shape change even though the dereference chain is unchanged.

Before comparing, --reviewed therefore pairs each `adrp Xn` with the memory
operand that uses Xn (folding any `add/sub Xn, Xn, #imm` in between), computes
the effective address, and rewrites the operand to a `[RELOC:Xn]` token. Two
rewritten operands are treated as equivalent when either
  (a) their effective addresses are equal -- a pure re-encoding or page/offset
      re-partition of the same location; or
  (b) their effective addresses differ but both are loads of the same width
      from a non-writable, non-executable section and the bytes read at those
      addresses are byte-for-byte identical -- the same immutable datum moved.
Anything else -- a different datum, a store to a different address, only one
side paired with an `adrp`, or a load whose bytes differ -- is not normalised.
For an adrp-paired operand that touches a non-writable, non-executable section,
a difference that is neither an equal address nor identical bytes is a
RELOC-DIFF and fails even under `--reviewed`: relocated read-only data must
resolve to the same value. Same-shaped displacement shifts in writable or
relocated sections (for example `.data.rel.ro` pointers) remain governed by the
pre-existing reviewed operand rule.

This is an equivalence rule over the effective address chain, not a whitelist:
it blesses no symbol, address, instruction index or mnemonic pair, and cannot
accept two operands unless both resolve to the same address or to identical
read-only bytes. The normalisation is applied only under `--reviewed`; strict
mode (no flag) keeps comparing the raw immediates, so the relocated constant
stays visible as a difference until a human requests the reviewed equivalence.
"""
import glob
import os
import re
import shutil
import struct
import subprocess
import sys

_TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
if _TOOLS_DIR not in sys.path:
    sys.path.insert(0, _TOOLS_DIR)
import important_trace as itrace

TARGETS = [
    ("owner_thread", [
        "ghostlock::race::owner_thread(void*)",
        "owner_thread(void*)",
        "owner_thread",
    ]),
    ("waiter_thread", [
        "ghostlock::race::waiter_thread(void*)",
        "waiter_thread(void*)",
        "waiter_thread",
    ]),
    ("consumer_thread", [
        "ghostlock::race::consumer_thread(void*)",
        "consumer_thread(void*)",
        "consumer_thread",
    ]),
    ("run_main_route_threads", [
        "ghostlock::race::run_main_route_threads(ghostlock::memory::WriteRequest const&)",
        "ghostlock::race::run_main_route_threads(ghostlock::memory::WriteRequest const*)",
        "ghostlock::race::run_main_route_threads(ghostlock::WriteRequest const*)",
        "run_main_route_threads(ghostlock::memory::WriteRequest const&)",
        "run_main_route_threads(ghostlock::memory::WriteRequest const*)",
        "run_main_route_threads(ghostlock::WriteRequest const*)",
        "run_main_route_threads",
    ]),
    ("do_kernel5_fake_lock_route", [
        "ghostlock::backend::cve_2026_43499::route::do_kernel5_fake_lock_route(ghostlock::memory::WriteRequest const*)",
        "ghostlock::backend::cve_2026_43499::route::do_kernel5_fake_lock_route(ghostlock::WriteRequest const*)",
        "ghostlock::route::do_kernel5_fake_lock_route(ghostlock::memory::WriteRequest const*)",
        "ghostlock::route::do_kernel5_fake_lock_route(ghostlock::WriteRequest const*)",
        "do_kernel5_fake_lock_route(ghostlock::memory::WriteRequest const*)",
        "do_kernel5_fake_lock_route(ghostlock::WriteRequest const*)",
        "do_kernel5_fake_lock_route",
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
        "do_one_write",
    ]),
    ("do_tcp_fake_lock_route", [
        "do_tcp_fake_lock_route",
    ]),
    ("do_pselect_fake_lock_route", [
        "do_pselect_fake_lock_route",
    ]),
    ("kernel5_resident_start", [
        "kernel5_resident_start",
    ]),
    ("kernel5_resident_write", [
        "kernel5_resident_write",
    ]),
    ("kernel5_resident_stop", [
        "kernel5_resident_stop",
    ]),
    ("prepare_skb_payload", [
        "prepare_skb_payload",
    ]),
    ("prepare_kernel_page", [
        "prepare_kernel_page",
    ]),
    ("prepare_good_kernel_page", [
        "prepare_good_kernel_page",
    ]),
    ("run_exploit", [
        "run_exploit",
    ]),
    ("spawn_crash_dump", [
        "spawn_crash_dump",
    ]),
    ("read_vendor_content", [
        "read_vendor_content",
    ]),
    ("do_one_write_cbc", [
        "do_one_write_cbc",
    ]),
    ("patch_file_cbc", [
        "patch_file_cbc",
    ]),
    ("patch_helper", [
        "patch_helper",
    ]),
    ("patch_ko", [
        "patch_ko",
    ]),
    ("patch_hook", [
        "patch_hook",
    ]),
    ("find_hook_target", [
        "find_hook_target",
    ]),
    ("createOrphanProcess", [
        "createOrphanProcess",
    ]),
    ("exploit", [
        "exploit",
    ]),
    ("setup", [
        "setup",
    ]),
    ("cleanup", [
        "cleanup",
    ]),
    ("compute_iv", [
        "compute_iv",
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
PARTIAL = False
COUNT_DELTA = None
IMPORTANT = False
IMPORTANT_ONLY = False
IMPORTANT_VERBOSE = False
IMPORTANT_IGNORE_NEW = False
IMPORTANT_IGNORE_REMOVED = False
IMPORTANT_JSON = None
IMPORTANT_SYMBOLS = set(itrace.DEFAULT_SYMBOLS)
IMPORTANT_DEPTH = 64
TRACE_INDIRECT = False


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


# ---- adrp-relative effective-address normalisation (--reviewed only) --------
# See the module docstring for the semantics. Pair adrp Xn, #page with the
# memory operand that uses Xn, fold any add/sub Xn, Xn, #imm, and rewrite the
# operand to [RELOC:Xn]. A pair is normalised only when the two effective
# addresses are equal, or when both are loads of the same width from a
# non-writable, non-executable section whose bytes are identical. An
# adrp-paired read-only operand that is not equivalent is a hard failure
# (RELOC-DIFF); every other difference is adjudicated as before. This is an
# equivalence rule over the dereference chain, never a symbol/address/index
# whitelist.

ADRP_RE = re.compile(r"^adrp\s+([wx]\d+)\s*,\s*#?(?:0x([0-9a-f]+)|(-?\d+))\b", re.I)
ADDSUB_RE = re.compile(
    r"^(?:add|sub)\s+([wx]\d+)\s*,\s*([wx]\d+)\s*,\s*#?(-?0x[0-9a-f]+|-?\d+)\b",
    re.I)
MEM_RE = re.compile(r"\[([^\]]*)\]")
DEST_RE = re.compile(r"\b([xwdsqbh])(\d+)\b")
REG_RE = re.compile(r"\b[wx](\d+)\b")

REG_WIDTH = {"b": 1, "h": 2, "w": 4, "s": 4, "d": 8, "x": 8, "q": 16}
LOAD_WIDTH_BY_MNEMONIC = {
    "ldrb": 1, "ldurb": 1, "ldtrb": 1, "ldrsb": 1, "ldursb": 1,
    "ldrh": 2, "ldurh": 2, "ldtrh": 2, "ldrsh": 2, "ldursh": 2,
    "ldrsw": 4, "ldursw": 4,
}
LOAD_MNEMONICS = {
    "ldr", "ldrb", "ldrh", "ldrsw", "ldur", "ldurb", "ldurh", "ldursb",
    "ldursw", "ldp", "ldnp", "ldar", "ldaxr", "ldxr", "ldtr", "ldtrb", "ldtrh",
}
STORE_MNEMONICS = {
    "str", "strb", "strh", "stur", "sturb", "sturh", "stp", "stnp", "stlr",
    "stxr", "sttr", "sttrb", "sttrh",
}
FIRST_OPERAND_DEST = {
    "mov", "movi", "movz", "movk", "movn", "and", "ands", "orr", "eor",
    "add", "adds", "sub", "subs", "mul", "madd", "msub", "neg", "mvn",
    "csel", "csinc", "cset", "cinc", "lsl", "lsr", "asr", "ubfm", "sbfm",
    "bfm", "ubfx", "sbfx", "bfi", "bfxil", "sxtw", "uxtw", "sxtb", "sxth",
    "sxtx", "uxtb", "uxth", "adr",
}


def canon_reg(name):
    """Canonicalise a general-purpose register to its 64-bit spelling."""
    match = re.fullmatch(r"[wx](\d+)", name.strip())
    return "x" + match.group(1) if match else name.strip()


def parse_int(text):
    text = text.strip().lstrip("#")
    negative = text.startswith("-")
    if text[:1] in "+-":
        text = text[1:]
    value = int(text, 16) if text.lower().startswith("0x") else int(text, 10)
    return -value if negative else value


def split_mnemonic(text):
    parts = text.split(None, 1)
    if not parts:
        return "", ""
    return parts[0].lower(), parts[1] if len(parts) > 1 else ""


def adrp_page(text):
    """Return the absolute page loaded by adrp Xn, #page, else None."""
    match = ADRP_RE.match(text)
    if not match:
        return None
    if match.group(2) is not None:
        return int(match.group(2), 16)
    return int(match.group(3), 10)


def written_regs(text):
    """Registers this instruction overwrites, to invalidate adrp bases."""
    mnemonic, rest = split_mnemonic(text)
    if mnemonic in STORE_MNEMONICS:
        return []
    before_memory = rest.split("[", 1)[0]
    if mnemonic in LOAD_MNEMONICS:
        return [canon_reg(reg) for reg in REG_RE.findall(before_memory)]
    if mnemonic in FIRST_OPERAND_DEST:
        first = before_memory.split(",", 1)[0].strip()
        return [canon_reg(first)] if re.fullmatch(r"[wx]\d+", first) else []
    return []


def load_width(text):
    """Bytes a load reads, or None when the instruction is not a load."""
    mnemonic, rest = split_mnemonic(text)
    if mnemonic in LOAD_WIDTH_BY_MNEMONIC:
        return LOAD_WIDTH_BY_MNEMONIC[mnemonic]
    if mnemonic not in LOAD_MNEMONICS:
        return None
    total = 0
    for letter, _digits in DEST_RE.findall(rest.split("[", 1)[0]):
        total += REG_WIDTH.get(letter, 0)
    return total or None


def track_bases(body):
    """Per-instruction map of register -> page address set by a preceding adrp.

    states[i] is the map entering instruction i, so a memory operand is
    resolved with the value of its base register at that point. A register is
    forgotten as soon as any other instruction overwrites it, so a stale page
    can never be paired with a later use.
    """
    states = []
    live = {}
    for text in body:
        states.append(dict(live))
        page = adrp_page(text)
        if page is not None:
            reg = ADRP_RE.match(text).group(1)
            live[canon_reg(reg)] = page
            continue
        match = ADDSUB_RE.match(text)
        if match:
            destination = canon_reg(match.group(1))
            source = canon_reg(match.group(2))
            if destination == source and destination in live:
                delta = parse_int(match.group(3))
                if match.group(0).lower().startswith("sub"):
                    delta = -delta
                live[destination] += delta
            else:
                live.pop(destination, None)
            continue
        for reg in written_regs(text):
            live.pop(reg, None)
    return states


def memory_operands(text, live):
    """adrp-derived memory operands as span/register/effective triples."""
    operands = []
    for match in MEM_RE.finditer(text):
        fields = [field.strip() for field in match.group(1).split(",")]
        if not fields:
            continue
        base = canon_reg(fields[0])
        if base not in live:
            continue
        offset = 0
        for field in fields[1:]:
            candidate = field.rstrip("!").strip()
            if re.fullmatch(r"#?-?(?:0x[0-9a-f]+|\d+)", candidate):
                offset = parse_int(candidate)
                break
        operands.append({"span": match.span(), "reg": base,
                         "eff": live[base] + offset})
    return operands


def rewrite_operands(text, operands):
    """Replace each adrp-derived memory operand with a [RELOC:Xn] token."""
    pieces = []
    last = 0
    for operand in sorted(operands, key=lambda item: item["span"][0]):
        start, end = operand["span"]
        pieces.append(text[last:start])
        pieces.append("[RELOC:%s]" % operand["reg"])
        last = end
    pieces.append(text[last:])
    return "".join(pieces)


class ElfImage:
    """Minimal ELF64 section reader: virtual address -> bytes and flags."""

    SHF_WRITE = 0x1
    SHF_EXECINSTR = 0x4

    def __init__(self, path):
        self.sections = []
        self.data = b""
        try:
            with open(path, "rb") as handle:
                data = handle.read()
        except OSError:
            return
        if len(data) < 0x40 or data[:4] != b"\x7fELF" or data[4] != 2:
            return
        self.data = data
        shoff = struct.unpack_from("<Q", data, 0x28)[0]
        shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x3a)
        if not shoff or not shentsize or not shnum or shstrndx >= shnum:
            return

        def section_header(index):
            return struct.unpack_from("<IIQQQQ", data, shoff + index * shentsize)

        _name, _type, _flags, _addr, str_off, str_size = section_header(shstrndx)
        strtab = data[str_off:str_off + str_size]
        for index in range(shnum):
            name, _type, flags, addr, offset, size = section_header(index)
            if addr == 0 or size == 0:
                continue
            label = strtab[name:].split(b"\0", 1)[0].decode("latin1")
            self.sections.append((label, addr, offset, size, flags))

    def read(self, vaddr, size):
        for label, addr, offset, section_size, flags in self.sections:
            if addr <= vaddr and vaddr + size <= addr + section_size:
                start = offset + (vaddr - addr)
                return label, self.data[start:start + size], flags
        return None


def section_flags(elf, vaddr):
    """Flags of the section containing vaddr, or None when unknown."""
    if not elf:
        return None
    resolved = elf.read(vaddr, 1)
    return resolved[2] if resolved else None


def relocation_resolve(base_body, cur_body, base_elf, cur_elf):
    """Rewrite equivalent adrp-relative operands.

    Returns (base, cur, notes, hard). A differing line is normalised (notes)
    only when pairing both sides to their adrp bases reproduces identical text
    and every operand pair resolves to the same location (equal effective
    address, or byte-identical read-only data). The indices in hard pair
    through adrp and touch a non-writable, non-executable section but do NOT
    resolve to the same location or the same bytes: that is a genuine data
    change and must fail even under --reviewed.
    """
    if len(base_body) != len(cur_body):
        return list(base_body), list(cur_body), [], []
    states_b = track_bases(base_body)
    states_c = track_bases(cur_body)
    normalised_b, normalised_c = list(base_body), list(cur_body)
    notes = []
    hard = []
    read_only_mask = ElfImage.SHF_WRITE | ElfImage.SHF_EXECINSTR
    for index in range(len(base_body)):
        line_b, line_c = base_body[index], cur_body[index]
        if line_b == line_c:
            continue
        ops_b = memory_operands(line_b, states_b[index])
        ops_c = memory_operands(line_c, states_c[index])
        if not ops_b and not ops_c:
            continue
        if len(ops_b) != len(ops_c):
            continue
        rewritten_b = rewrite_operands(line_b, ops_b)
        rewritten_c = rewrite_operands(line_c, ops_c)
        if rewritten_b != rewritten_c:
            continue
        detail = []
        hard_hit = False
        for operand_b, operand_c in zip(ops_b, ops_c):
            if operand_b["reg"] != operand_c["reg"]:
                detail = []
                break
            if operand_b["eff"] == operand_c["eff"]:
                detail.append((operand_b, operand_c, "same-address"))
                continue
            flags_b = section_flags(base_elf, operand_b["eff"])
            flags_c = section_flags(cur_elf, operand_c["eff"])
            read_only = ((flags_b is not None and not flags_b & read_only_mask)
                         or (flags_c is not None and not flags_c & read_only_mask))
            width = load_width(line_b)
            if width is not None and width == load_width(line_c) \
                    and base_elf and cur_elf:
                resolved_b = base_elf.read(operand_b["eff"], width)
                resolved_c = cur_elf.read(operand_c["eff"], width)
                if resolved_b and resolved_c \
                        and not resolved_b[2] & read_only_mask \
                        and not resolved_c[2] & read_only_mask \
                        and resolved_b[1] == resolved_c[1]:
                    detail.append((operand_b, operand_c, "same-readonly-datum"))
                    continue
            if read_only:
                hard_hit = True
            break
        if hard_hit:
            hard.append(index)
            continue
        if not detail:
            continue
        normalised_b[index] = rewritten_b
        normalised_c[index] = rewritten_c
        notes.append((index, line_b, line_c, detail))
    return normalised_b, normalised_c, notes, hard


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


def call_targets(lines):
    """Return the symbol targets of direct `bl` instructions, in order."""
    targets = []
    for line in lines:
        match = re.match(r"bl\s+0x[0-9a-f]+\s+<([^>]+)>", line)
        if match:
            targets.append(match.group(1))
    return targets


def is_subsequence(needle, haystack):
    """True when every call in needle appears in haystack in the same order."""
    it = iter(haystack)
    return all(item in it for item in needle)


def main():
    global OBJDUMP, GLOBAL_SHIFT, PARTIAL, COUNT_DELTA
    global IMPORTANT, IMPORTANT_ONLY, IMPORTANT_VERBOSE, IMPORTANT_JSON
    global IMPORTANT_IGNORE_NEW, IMPORTANT_IGNORE_REMOVED
    global IMPORTANT_SYMBOLS, IMPORTANT_DEPTH, TRACE_INDIRECT
    argv = sys.argv[1:]

    def take_value(flag):
        if flag not in argv:
            return None
        index = argv.index(flag)
        if index + 1 >= len(argv):
            sys.exit(f"cmp_disasm.py: {flag} needs a value")
        value = argv[index + 1]
        del argv[index:index + 2]
        return value

    count_delta = take_value("--count-delta")
    COUNT_DELTA = int(count_delta) if count_delta is not None else None
    json_path = take_value("--important-json")
    symbols_path = take_value("--important-symbols")
    depth = take_value("--trace-depth")
    if depth is not None:
        IMPORTANT_DEPTH = int(depth)
    IMPORTANT_JSON = json_path
    if symbols_path:
        with open(symbols_path) as handle:
            for line in handle:
                line = line.strip()
                if line and not line.startswith("#"):
                    IMPORTANT_SYMBOLS.add(itrace.symbol_key(line))

    IMPORTANT_ONLY = "--important-only" in argv
    IMPORTANT = IMPORTANT_ONLY or "--important" in argv
    IMPORTANT_VERBOSE = "--important-verbose" in argv
    IMPORTANT_IGNORE_NEW = "--important-ignore-new" in argv
    IMPORTANT_IGNORE_REMOVED = "--important-ignore-removed" in argv
    TRACE_INDIRECT = "--trace-indirect" in argv
    GLOBAL_SHIFT = "--reviewed" in argv
    PARTIAL = "--partial" in argv
    flags = {"--reviewed", "--partial", "--important", "--important-only",
             "--important-verbose", "--important-ignore-new",
             "--important-ignore-removed", "--trace-indirect"}
    paths = [a for a in argv if a not in flags]
    if len(paths) != 2:
        sys.exit(main.__doc__ or "usage: cmp_disasm.py [--reviewed] [--partial] "
                 "[--count-delta N] [--important] [--important-only] "
                 "[--important-verbose] [--important-json PATH] "
                 "[--important-symbols PATH] [--trace-depth N] <baseline> <candidate>")
    base_path, cur_path = paths
    OBJDUMP = find_objdump()
    base, cur = disassemble(base_path), disassemble(cur_path)
    base_elf = ElfImage(base_path) if GLOBAL_SHIFT else None
    cur_elf = ElfImage(cur_path) if GLOBAL_SHIFT else None
    failed = 0
    targets = [] if IMPORTANT_ONLY else TARGETS
    for label, candidates in targets:
        base_name = resolve(base, candidates)
        cur_name = resolve(cur, candidates)
        if base_name is None or cur_name is None:
            if PARTIAL and base_name is None and cur_name is None:
                print(f"SKIP {label}: absent from both binaries")
                continue
            print(f"MISSING {label}: base={base_name is not None} cur={cur_name is not None}")
            failed += 1
            continue
        b, c = base[base_name], cur[cur_name]
        if len(b) != len(c):
            delta = abs(len(c) - len(b))
            if COUNT_DELTA is not None and delta <= COUNT_DELTA and \
                    is_subsequence(call_targets(b), call_targets(c)):
                print(f"COUNT-DELTA {label}: base={len(b)} cur={len(c)} "
                      f"delta={delta} calls=subsequence")
                continue
            print(f"DIFF {label}: instruction count base={len(b)} cur={len(c)}")
            failed += 1
            continue
        if GLOBAL_SHIFT:
            b, c, reloc_notes, hard = relocation_resolve(b, c, base_elf, cur_elf)
            if reloc_notes:
                print(f"RELOC-SHIFT {label}: {len(reloc_notes)} adrp-relative memory "
                      f"operand(s) resolved to an equivalent location")
                for i, line_b, line_c, detail in reloc_notes[:5]:
                    print(f"  [{i}] base: {line_b}")
                    print(f"  [{i}] cur:  {line_c}")
                    for operand_b, operand_c, reason in detail:
                        print(f"       effective address base={operand_b['eff']:#x} "
                              f"cur={operand_c['eff']:#x} ({reason})")
            if hard:
                failed += 1
                print(f"RELOC-DIFF {label}: {len(hard)} adrp-relative read-only "
                      f"operand(s) resolve to different data")
                for i in sorted(hard)[:5]:
                    print(f"  [{i}] base: {b[i]}")
                    print(f"  [{i}] cur:  {c[i]}")
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

    if IMPORTANT:
        base_analysis = itrace.analyze(base_path, OBJDUMP, IMPORTANT_SYMBOLS,
                                       IMPORTANT_DEPTH, TRACE_INDIRECT)
        cur_analysis = itrace.analyze(cur_path, OBJDUMP, IMPORTANT_SYMBOLS,
                                      IMPORTANT_DEPTH, TRACE_INDIRECT)
        important_ok, important_lines = itrace.compare(
            base_analysis, cur_analysis, IMPORTANT_VERBOSE,
            IMPORTANT_IGNORE_NEW, IMPORTANT_IGNORE_REMOVED)
        for line in important_lines:
            print(line)
        if IMPORTANT_VERBOSE:
            print("--- baseline important trees ---")
            for line in itrace.format_analysis(base_analysis):
                print(line)
            print("--- candidate important trees ---")
            for line in itrace.format_analysis(cur_analysis):
                print(line)
        if IMPORTANT_JSON:
            itrace.write_json(IMPORTANT_JSON, base_analysis, cur_analysis)
            print(f"IMPORTANT-JSON: {IMPORTANT_JSON}")
        if not important_ok:
            failed += 1

    print("RESULT:", "FAIL" if failed else "PASS")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
