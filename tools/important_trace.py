#!/usr/bin/env python3
"""Important-call/syscall extraction and argument-chain tracing for cmp_disasm.

功能总览
========
1. 重要指令筛选
   - 直接 bl 到默认/自定义符号表（futex/socket/mmap/ioctl/pthread/syscall…）；
   - svc #0 始终记录；blr 可选（--trace-indirect）；
   - 按函数记录数量、顺序、偏移、kind、target、参数寄存器。
2. 参数链追踪
   - 从重要指令前一条反向扫描目标寄存器；
   - 根：arg / fixed / frame / frame-load / load / computed / call-return /
     syscall-return / system-register / unknown；
   - 每一次写目标寄存器的指令都作为 step（offset + text）；
   - 支持 mov/movz/movk/adrp+add/ldr/ldp/ldur/csel/madd/extr 等；
   - xzr 归固定 0；sp/x29 归栈帧；adrp 链按地址链归一化。
3. 比较与输出
   - compare() 逐函数比较数量、顺序、参数根与修改链；
   - 支持 ignore-new/ignore-removed；
   - format_analysis() 打印参数树；write_json() 输出双份分析。
4. 边界
   - 反向线性扫描，不区分控制流路径/循环；
   - 不跨函数展开；只覆盖整数 x0..x7；
   - ldp 只按一个内存操作数记 load，但按目的槽位加偏移。

The module is intentionally independent of cmp_disasm's comparison modes so the
existing behaviour stays unchanged.

Public entry points:
    disassemble_rich(path, objdump) -> {function: [Instr, ...]}
    analyze(path, objdump, symbols, max_depth, include_indirect) -> dict
    compare(base, candidate, verbose) -> (ok, lines)
"""
from __future__ import annotations

import json
import re
import subprocess
from typing import Dict, Iterable, List, Optional, Sequence, Set, Tuple

DEFAULT_SYMBOLS = {
    "futex_op", "sched_setattr_tid", "pin_to_core", "disable_rseq_for_thread",
    "pthread_create", "pthread_join", "pthread_sigmask", "sigaction",
    "socket", "setsockopt", "getsockopt", "sendmsg", "recvmsg", "send", "recv",
    "splice", "vmsplice", "mmap", "munmap", "mprotect", "ioctl", "open",
    "openat", "close", "read", "write", "pread", "pwrite", "pread64", "pwrite64",
    "clone", "fork", "vfork", "execve", "execl", "waitpid", "wait4", "kill",
    "tgkill", "syscall", "prctl", "fallocate", "memfd_create", "timerfd_create",
    "pselect", "select", "dup2", "fcntl", "chmod", "uname", "getuid", "getpid",
    "gettid", "sched_setaffinity", "sched_getaffinity", "sched_setscheduler",
    "perf_event_open", "setrlimit", "capset", "init_module", "finit_module",
    "delete_module", "mount", "umount", "ptrace", "process_vm_readv",
    "process_vm_writev", "shmget", "shmat", "sysconf", "access", "readlinkat",
}

ARG_REGS = tuple(f"x{i}" for i in range(8))
DATA_DEST = {
    "mov", "movz", "movn", "movk", "adr", "adrp", "add", "adds", "sub", "subs",
    "and", "ands", "orr", "eor", "lsl", "lsr", "asr", "ror", "mul", "madd",
    "msub", "smull", "umull", "sdiv", "udiv", "csel", "csinc", "csinv", "csneg",
    "cset", "csetm", "cinc", "neg", "mvn", "ubfm", "sbfm", "bfm", "ubfx",
    "sbfx", "bfi", "bfxil", "sxtb", "sxth", "sxtw", "uxtb", "uxth", "rev",
    "rev16", "rev32", "clz", "cls", "rbit", "extr", "mrs", "ldr", "ldrb",
    "ldrh", "ldrsw", "ldur", "ldurb", "ldurh", "ldursb", "ldursw", "ldp",
    "ldnp", "ldar", "ldaxr", "ldxr", "ldtr", "ldtrb", "ldtrh",
}
BRANCH_LINK = {"bl", "blr"}
ARG_COUNT = {
    "close": 1, "getuid": 0, "getpid": 0, "gettid": 0, "fork": 0, "vfork": 0,
    "pthread_join": 2, "pthread_create": 4, "pthread_sigmask": 3,
    "sigaction": 3, "socket": 3, "setsockopt": 5, "getsockopt": 5,
    "sendmsg": 3, "recvmsg": 3, "send": 4, "recv": 4, "splice": 6,
    "vmsplice": 4, "mmap": 6, "munmap": 2, "mprotect": 3, "ioctl": 3,
    "open": 3, "openat": 4, "read": 3, "write": 3, "pread": 4, "pwrite": 4,
    "pread64": 4, "pwrite64": 4, "clone": 5, "execve": 3, "execl": 3,
    "waitpid": 3, "wait4": 4, "kill": 2, "tgkill": 3, "syscall": 7,
    "prctl": 5, "fallocate": 4, "memfd_create": 2, "timerfd_create": 2,
    "pselect": 6, "select": 5, "dup2": 2, "fcntl": 3, "chmod": 2,
    "uname": 1, "sched_setaffinity": 3, "sched_getaffinity": 3,
    "sched_setscheduler": 3, "perf_event_open": 5, "setrlimit": 2,
    "capset": 2, "init_module": 3, "finit_module": 3, "delete_module": 2,
    "mount": 5, "umount": 2, "ptrace": 4, "process_vm_readv": 6,
    "process_vm_writev": 6, "access": 2, "readlinkat": 4, "sysconf": 1,
    "futex_op": 6, "sched_setattr_tid": 2, "pin_to_core": 1,
    "disable_rseq_for_thread": 0,
}



class Instr:
    """One decoded instruction with its virtual address."""

    __slots__ = ("addr", "text")

    def __init__(self, addr: int, text: str) -> None:
        self.addr = addr
        self.text = text


def disassemble_rich(path: str, objdump: str) -> Dict[str, List[Instr]]:
    """Disassemble one binary into function -> instructions (address retained)."""
    out = subprocess.run(
        [objdump, "-d", "--no-show-raw-insn", "--demangle", path],
        capture_output=True, text=True, check=True).stdout
    funcs: Dict[str, List[Instr]] = {}
    current: Optional[str] = None
    header = re.compile(r"^([0-9a-f]+) <(.+)>:$")
    body = re.compile(r"^\s*([0-9a-f]+):\s*(.*)$")
    for line in out.splitlines():
        match = header.match(line)
        if match:
            current = match.group(2)
            funcs.setdefault(current, [])
            continue
        if current is None:
            continue
        match = body.match(line)
        if match:
            text = match.group(2).strip()
            if text:
                funcs[current].append(Instr(int(match.group(1), 16), text))
    return funcs


def split_mnemonic(text: str) -> Tuple[str, str]:
    """Split an instruction into lower-case mnemonic and operand text."""
    text = text.split("//", 1)[0].strip()
    if not text:
        return "", ""
    parts = text.split(None, 1)
    return parts[0].lower(), parts[1] if len(parts) > 1 else ""


def split_operands(text: str) -> List[str]:
    """Split by commas that are not inside [] or () brackets."""
    out: List[str] = []
    depth = 0
    current = ""
    for ch in text:
        if ch in "[(":
            depth += 1
        elif ch in "])":
            depth = max(0, depth - 1)
        if ch == "," and depth == 0:
            out.append(current.strip())
            current = ""
        else:
            current += ch
    if current.strip():
        out.append(current.strip())
    return out


def canon_reg(text: str) -> Optional[str]:
    """Canonicalise an integer register to xN, sp, xzr, or an alias."""
    token = text.strip().lstrip("#").rstrip("!")
    token = token.split()[0] if token.split() else token
    aliases = {"fp": "x29", "lr": "x30", "ip0": "x16", "ip1": "x17",
               "wsp": "sp", "wzr": "xzr"}
    token = aliases.get(token, token)
    match = re.fullmatch(r"[wx](\d+)", token)
    if match:
        return "x" + match.group(1)
    if token in ("sp", "xzr"):
        return token
    return None


def parse_imm(text: str) -> Optional[int]:
    """Parse #imm / #-0x.. / bare immedates; return None for non-immediates."""
    token = text.strip().lstrip("#")
    token = token.replace(":lo12:", "")
    match = re.fullmatch(r"(-?)(0x[0-9a-f]+|\d+)", token)
    if not match:
        return None
    value = int(match.group(2), 16) if match.group(2).lower().startswith("0x") else int(match.group(2))
    return -value if match.group(1) else value


def mem_operand(text: str) -> Optional[Dict[str, object]]:
    """Parse [base, #off] / [base, index, lsl #n] into a small description."""
    match = re.search(r"\[([^\]]+)\]", text)
    if not match:
        return None
    inner = split_operands(match.group(1))
    if not inner:
        return None
    base = canon_reg(inner[0])
    offset = parse_imm(inner[1]) if len(inner) > 1 and parse_imm(inner[1]) is not None else 0
    index = None
    shift = 0
    for item in inner[1:]:
        reg = canon_reg(item)
        if reg:
            index = reg
        shift_match = re.search(r"lsl\s+#?(\d+)", item)
        if shift_match:
            shift = int(shift_match.group(1))
    return {"base": base, "offset": offset or 0, "index": index, "shift": shift}


def dest_regs(mnemonic: str, operands: Sequence[str]) -> List[str]:
    """Return the registers written by one instruction."""
    if mnemonic in ("bl", "blr", "svc"):
        return ["x0"]
    if mnemonic in ("ldp", "ldnp"):
        return [r for r in (canon_reg(op) for op in operands[:2]) if r]
    if mnemonic in DATA_DEST and operands:
        reg = canon_reg(operands[0])
        return [reg] if reg else []
    return []


def target_symbol(text: str) -> Optional[str]:
    """Extract <symbol> from a branch instruction."""
    match = re.search(r"<([^>]+)>", text)
    return match.group(1) if match else None


def symbol_key(symbol: str) -> str:
    """Strip @plt and C++ argument annotation for the default symbol set."""
    return symbol.split("@", 1)[0].split("(", 1)[0]


def step(ins: Instr) -> Dict[str, str]:
    return {"offset": hex(ins.addr), "text": ins.text}


def root_arg(reg: str, steps: List[Dict[str, str]]) -> Dict[str, object]:
    return {"kind": "arg", "value": reg, "steps": steps, "children": []}


def has_reloc(trace: Dict[str, object]) -> bool:
    """True when the trace contains an adrp/adr address root."""
    for item in trace.get("steps", []):
        text = str(item.get("text", ""))
        if text.startswith("adrp") or text.startswith("adr"):
            return True
    return any(has_reloc(child) for child in trace.get("children", []))


def trace_register(instrs: Sequence[Instr], index: int, reg: str,
                   max_depth: int = 64,
                   seen: Optional[Set[Tuple[int, str]]] = None) -> Dict[str, object]:
    """Trace one register backwards to its root and every defining step."""
    if reg == "xzr":
        return {"kind": "fixed", "value": "0x0", "steps": [], "children": []}
    if reg in ("sp", "x29"):
        return {"kind": "frame", "value": reg, "steps": [], "children": []}
    if index < 0:
        return root_arg(reg, [])
    if max_depth <= 0:
        return {"kind": "unknown", "value": "depth-limit", "steps": [], "children": []}
    seen = set() if seen is None else seen
    key = (index, reg)
    if key in seen:
        return {"kind": "unknown", "value": "cycle", "steps": [], "children": []}
    seen = set(seen)
    seen.add(key)

    ins = instrs[index]
    mnemonic, rest = split_mnemonic(ins.text)
    operands = split_operands(rest)
    if reg not in dest_regs(mnemonic, operands):
        return trace_register(instrs, index - 1, reg, max_depth - 1, seen)
    current_step = step(ins)

    def child(source: str) -> Dict[str, object]:
        source_reg = canon_reg(source)
        if source_reg == "xzr":
            return {"kind": "fixed", "value": "0x0", "steps": [], "children": []}
        if source_reg in ("sp", "x29"):
            return {"kind": "frame", "value": source_reg, "steps": [], "children": []}
        if source_reg:
            return trace_register(instrs, index - 1, source_reg, max_depth - 1, seen)
        immediate = parse_imm(source)
        if immediate is not None:
            return {"kind": "fixed", "value": hex(immediate & 0xFFFFFFFFFFFFFFFF),
                    "steps": [], "children": []}
        return {"kind": "unknown", "value": source, "steps": [], "children": []}

    if mnemonic in ("bl", "blr"):
        target = target_symbol(ins.text) or (canon_reg(operands[0]) if operands else "?")
        node = {"kind": "call-return", "value": target, "steps": [current_step],
                "children": []}
        if mnemonic == "blr" and operands:
            node["children"] = [child(operands[0])]
        return node

    if mnemonic == "svc":
        return {"kind": "syscall-return", "value": "svc", "steps": [current_step],
                "children": []}

    if mnemonic in ("mov", "orr", "and", "add", "sub", "eor", "lsl", "lsr",
                    "asr", "ror", "mul", "sdiv", "udiv", "sxtw", "uxtw",
                    "sxtb", "sxth", "uxtb", "uxth", "rev", "rev16", "rev32",
                    "clz", "cls", "rbit", "neg", "mvn", "csel", "csinc",
                    "csinv", "csneg", "cinc", "cset", "csetm", "madd", "msub",
                    "smull", "umull", "extr"):
        if mnemonic in ("madd", "msub"):
            sources = operands[1:4]
        elif mnemonic == "extr":
            sources = operands[1:3]
        elif mnemonic in ("csel", "csinc", "csinv", "csneg"):
            sources = operands[1:3]
        elif mnemonic in ("cset", "csetm"):
            sources = []
        else:
            sources = operands[1:]
        if mnemonic == "orr" and len(sources) >= 2 and canon_reg(sources[0]) == "xzr":
            sources = sources[1:]
        children = [child(source) for source in sources]
        if len(children) == 1:
            node = dict(children[0])
            node["steps"] = list(node.get("steps", [])) + [current_step]
            return node
        reloc = mnemonic in ("add", "sub") and len(children) >= 2 \
            and (has_reloc(children[0]) or children[0].get("kind") == "frame")
        if reloc and children[1].get("kind") == "fixed":
            children[1] = dict(children[1])
            children[1]["value"] = "<addr>"
        return {"kind": "computed", "value": mnemonic, "steps": [current_step],
                "children": children, "reloc": reloc}

    if mnemonic in ("movz", "movn", "movk"):
        immediate = parse_imm(operands[1]) if len(operands) > 1 else None
        value = 0 if immediate is None else immediate
        if mnemonic == "movn":
            value = (~value) & 0xFFFFFFFFFFFFFFFF
        if mnemonic == "movk":
            shift = 0
            if len(operands) > 2:
                shift_match = re.search(r"#(\d+)", operands[2])
                shift = int(shift_match.group(1)) if shift_match else 0
            previous = trace_register(instrs, index - 1, reg, max_depth - 1, seen)
            if previous.get("kind") == "fixed":
                base = int(str(previous.get("value")), 16)
                value = (base & ~(0xFFFF << shift)) | ((value & 0xFFFF) << shift)
                return {"kind": "fixed", "value": hex(value),
                        "steps": list(previous.get("steps", [])) + [current_step],
                        "children": []}
            return {"kind": "computed", "value": "movk", "steps": [current_step],
                    "children": [previous, {"kind": "fixed", "value": hex((value & 0xFFFF) << shift), "steps": [], "children": []}]}
        return {"kind": "fixed", "value": hex(value), "steps": [current_step],
                "children": []}

    if mnemonic in ("adr", "adrp"):
        immediate = parse_imm(operands[1]) if len(operands) > 1 else None
        return {"kind": "fixed", "value": hex(immediate or 0), "steps": [current_step],
                "children": []}

    if mnemonic.startswith("ldr") or mnemonic.startswith("ldur") or mnemonic in ("ldp", "ldnp", "ldar", "ldxr", "ldaxr"):
        mem = mem_operand(rest)
        if mem is None:
            return {"kind": "unknown", "value": ins.text, "steps": [current_step],
                    "children": []}
        base = mem.get("base")
        dest_slot = 0
        if mnemonic in ("ldp", "ldnp"):
            dests = [canon_reg(op) for op in operands[:2]]
            if reg in dests:
                dest_slot = dests.index(reg)
        cell = 4 if operands[dest_slot].lstrip().startswith("w") else 8
        offset = int(mem.get("offset", 0)) + dest_slot * cell
        if base in ("sp", "x29"):
            return {"kind": "frame-load", "value": f"[{base}]",
                    "steps": [current_step], "children": [], "reloc": True}
        children = []
        if isinstance(base, str) and base not in ("xzr", "sp"):
            children.append(trace_register(instrs, index - 1, base, max_depth - 1, seen))
        reloc = bool(children) and (has_reloc(children[0]) or children[0].get("kind") == "frame")
        value = f"[{base}, #{offset}]" if not reloc else "[reloc]"
        return {"kind": "load", "value": value, "steps": [current_step],
                "children": children, "reloc": reloc}

    if mnemonic == "mrs":
        return {"kind": "system-register", "value": operands[1] if len(operands) > 1 else "?",
                "steps": [current_step], "children": []}

    if mnemonic in ("ubfx", "sbfx", "bfxil", "bfi", "ubfm", "sbfm", "bfm"):
        children = [child(operands[1])] if len(operands) > 1 else []
        return {"kind": "computed", "value": mnemonic, "steps": [current_step],
                "children": children}

    return {"kind": "unknown", "value": ins.text, "steps": [current_step],
            "children": []}


def important_for_function(name: str, instrs: Sequence[Instr],
                           symbols: Set[str], max_depth: int,
                           include_indirect: bool) -> List[Dict[str, object]]:
    """Extract important calls/syscalls and trace their argument registers."""
    found: List[Dict[str, object]] = []
    for index, ins in enumerate(instrs):
        mnemonic, rest = split_mnemonic(ins.text)
        kind = None
        target = None
        args: Dict[str, object] = {}
        if mnemonic == "svc":
            kind = "syscall"
            target = "svc"
            for reg in ("x0", "x1", "x2", "x3", "x4", "x5", "x8"):
                args[reg] = trace_register(instrs, index - 1, reg, max_depth)
        elif mnemonic in BRANCH_LINK:
            raw_target = target_symbol(ins.text)
            if mnemonic == "blr":
                if not include_indirect:
                    continue
                kind = "indirect-call"
                target = raw_target or (canon_reg(split_operands(rest)[0]) if split_operands(rest) else "?")
            else:
                if not raw_target or symbol_key(raw_target) not in symbols:
                    continue
                kind = "call"
                target = raw_target
            count = ARG_COUNT.get(symbol_key(target), 6) if kind == "call" else 6
            for reg in ARG_REGS[:count]:
                args[reg] = trace_register(instrs, index - 1, reg, max_depth)
        if kind is None:
            continue
        found.append({"index": len(found) + 1, "offset": hex(ins.addr),
                      "kind": kind, "target": target, "args": args})
    return found


def analyze(path: str, objdump: str, symbols: Optional[Set[str]] = None,
            max_depth: int = 64, include_indirect: bool = False) -> Dict[str, object]:
    """Analyze one binary and return important instructions per function."""
    funcs = disassemble_rich(path, objdump)
    symbols = set(DEFAULT_SYMBOLS if symbols is None else symbols)
    result = {"binary": path, "functions": {}}
    for name, instrs in funcs.items():
        important = important_for_function(name, instrs, symbols, max_depth, include_indirect)
        if important:
            result["functions"][name] = important
    return result


def norm_text(text: str) -> str:
    """Normalise absolute addresses while keeping immediates and symbols."""
    text = re.sub(r"0x[0-9a-f]+ <", "<", text)
    parts = re.split(r"(<[^>]*>)", text)
    for i in range(0, len(parts), 2):
        parts[i] = re.sub(r"(?<!#)(?<!#-)0x[0-9a-f]+", "0xH", parts[i])
    return "".join(parts)


def norm_reloc_text(text: str) -> str:
    """Normalise address offsets in an adrp/frame-relative chain."""
    text = norm_text(text)
    text = re.sub(r"#-?0x[0-9a-f]+", "#0xH", text)
    return re.sub(r"\[(sp|x29), #0xH\]", r"[\1]", text)


def trace_equal(base: Dict[str, object], cur: Dict[str, object]) -> bool:
    """Compare two trace trees; adrp-relative chains ignore relocated offsets."""
    brel = bool(base.get("reloc")) or base.get("kind") == "frame-load"
    crel = bool(cur.get("reloc")) or cur.get("kind") == "frame-load"
    if brel != crel:
        return False
    if base.get("kind") != cur.get("kind"):
        return False
    if not brel and norm_text(str(base.get("value"))) != norm_text(str(cur.get("value"))):
        return False
    normalise = norm_reloc_text if brel else norm_text
    bsteps = [normalise(str(s.get("text"))) for s in base.get("steps", [])]
    csteps = [normalise(str(s.get("text"))) for s in cur.get("steps", [])]
    if bsteps != csteps:
        return False
    bchildren = base.get("children", [])
    cchildren = cur.get("children", [])
    if len(bchildren) != len(cchildren):
        return False
    if brel:
        for b, c in zip(bchildren, cchildren):
            if b.get("kind") == "fixed" and c.get("kind") == "fixed":
                continue
            if not trace_equal(b, c):
                return False
        return True
    return all(trace_equal(b, c) for b, c in zip(bchildren, cchildren))


def _summary(trace: Dict[str, object]) -> str:
    kind = str(trace.get("kind"))
    value = str(trace.get("value"))
    steps = len(trace.get("steps", []))
    return f"{kind}:{value} ({steps} steps)"


def compare(base: Dict[str, object], candidate: Dict[str, object],
            verbose: bool = False, ignore_new: bool = False,
            ignore_removed: bool = False) -> Tuple[bool, List[str]]:
    """Compare important-instruction counts, order, and argument traces."""
    lines: List[str] = []
    ok = True
    bfuncs = base.get("functions", {})
    cfuncs = candidate.get("functions", {})
    total_b = sum(len(items) for items in bfuncs.values())
    total_c = sum(len(items) for items in cfuncs.values())
    lines.append(f"IMPORTANT-SUMMARY functions={len(set(bfuncs) | set(cfuncs))} "
                 f"base={total_b} cur={total_c}")
    for name in sorted(set(bfuncs) | set(cfuncs)):
        b = bfuncs.get(name, [])
        c = cfuncs.get(name, [])
        if not b and c:
            status = "OK (new)" if ignore_new else "FAIL"
            lines.append(f"IMPORTANT-NEW {name}: cur={len(c)} {status}")
            ok = ok and ignore_new
        elif b and not c:
            status = "OK (removed)" if ignore_removed else "FAIL"
            lines.append(f"IMPORTANT-REMOVED {name}: base={len(b)} {status}")
            ok = ok and ignore_removed
        elif len(b) != len(c):
            lines.append(f"IMPORTANT-COUNT {name}: base={len(b)} cur={len(c)} FAIL")
            ok = False
        else:
            lines.append(f"IMPORTANT-COUNT {name}: {len(b)} OK")
        for index in range(min(len(b), len(c))):
            bi, ci = b[index], c[index]
            label = f"#{index + 1} {bi.get('kind')}:{bi.get('target')}"
            if bi.get("kind") != ci.get("kind") or bi.get("target") != ci.get("target"):
                lines.append(f"IMPORTANT-ORDER {name} {label} FAIL cur={ci.get('kind')}:{ci.get('target')}")
                ok = False
                continue
            lines.append(f"IMPORTANT-ORDER {name} {label} OK")
            bargs, cargs = bi.get("args", {}), ci.get("args", {})
            for arg in sorted(set(bargs) | set(cargs)):
                bt, ct = bargs.get(arg), cargs.get(arg)
                if bt is None or ct is None:
                    lines.append(f"IMPORTANT-TRACE {name} {label} {arg} FAIL missing")
                    ok = False
                    continue
                if not trace_equal(bt, ct):
                    lines.append(
                        f"IMPORTANT-TRACE {name} {label} {arg} DIFF "
                        f"base={_summary(bt)} cur={_summary(ct)}")
                    ok = False
                elif verbose:
                    lines.append(
                        f"IMPORTANT-TRACE {name} {label} {arg} OK {_summary(bt)}")
    lines.append("IMPORTANT-RESULT: " + ("PASS" if ok else "FAIL"))
    return ok, lines


def format_tree(trace: Dict[str, object], prefix: str = "") -> List[str]:
    """Render one trace tree as indented text."""
    lines = [f"{prefix}{trace.get('kind')}:{trace.get('value')}"]
    for item in trace.get("steps", []):
        lines.append(f"{prefix}  {item.get('offset')}: {item.get('text')}")
    for child in trace.get("children", []):
        lines.extend(format_tree(child, prefix + "  "))
    return lines


def format_analysis(analysis: Dict[str, object]) -> List[str]:
    """Render all important instructions and argument trees."""
    lines: List[str] = []
    for name in sorted(analysis.get("functions", {})):
        for item in analysis["functions"][name]:
            lines.append(f"IMPORTANT-TREE {name} #{item['index']} "
                         f"{item['kind']}:{item['target']} @{item['offset']}")
            for arg in sorted(item.get("args", {})):
                lines.append(f"  {arg} <-")
                lines.extend(format_tree(item["args"][arg], "    "))
    return lines


def write_json(path: str, base: Dict[str, object], candidate: Dict[str, object]) -> None:
    """Write both analyses as one JSON document."""
    with open(path, "w") as handle:
        json.dump({"baseline": base, "candidate": candidate}, handle,
                  indent=2, sort_keys=True)
        handle.write("\n")
