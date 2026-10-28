#!/usr/bin/env python3
"""JNI 导出符号 host gate（task-62，防 N01 再犯）。

做三件事：
1. 解析 `app/src/main/kotlin/com/ghostlock/app/jni/*.kt` 中的 `external fun`，
   按所属 object/class + 包名生成期望 JNI 符号
   `Java_<pkg>_<Class>_<method>`（JNI 名称转义按规范；本仓当前无重载）。
2. 用 NDK `llvm-nm -D --defined-only` 从 Debug/Release 的
   `libghostlock43499.so` 取动态导出符号。
3. 断言：期望集合 ⊆ 产物集合；缺失打印清单并 EXIT≠0。Debug 与 Release 都跑。

仅验证 `Native43499Jni` 与 `VictimTransportJni` 两个 JNI 类（其他 JNI 类属于
`libghostlock_general`/`libghostlock98260jni` 等其它产物，不在本 gate 范围）。

用法（host；无需设备）：
    ./gradlew :app:externalNativeBuildDebug :app:externalNativeBuildRelease
    python3 tools/verify_jni_exports.py

可选：--ndk/--nm/--debug-lib/--release-lib/--kotlin-dir。
退出码：0=PASS；1=缺符号；2=环境/产物缺失。
"""

from __future__ import annotations

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
KOTLIN_DIR = REPO_ROOT / "app" / "src" / "main" / "kotlin" / "com" / "ghostlock" / "app" / "jni"
TARGET_CLASSES = ("Native43499Jni", "VictimTransportJni")
LIB_NAME = "libghostlock43499.so"
PACKAGE_RE = re.compile(r"^\s*package\s+([\w.]+)\s*$")
CLASS_RE = re.compile(
    r"^\s*(?:(?:internal|private|public|open|abstract|final|data)\s+)*"
    r"(?:object|class)\s+([A-Za-z_][A-Za-z0-9_]*)",
)
EXTERNAL_RE = re.compile(r"^\s*(?:@\w+(?:\([^)]*\))?\s+)*external\s+fun\s+([A-Za-z_][A-Za-z0-9_]*)\b")
SYMBOL_RE = re.compile(r"Java_[A-Za-z0-9_]+")


class GateError(RuntimeError):
    """环境/解析错误。"""


def log(message: str) -> None:
    print(message, flush=True)


def jni_escape(identifier: str) -> str:
    """JNI 名称转义（规范 §Resolving Native Method Names）。"""
    output: list[str] = []
    for char in identifier:
        if char == "_":
            output.append("_1")
        elif char == ";":
            output.append("_2")
        elif char == "[":
            output.append("_3")
        elif char.isalnum() and char.isascii():
            output.append(char)
        else:
            output.append(f"_0{ord(char):04x}")
    return "".join(output)


def expected_symbol(package: str, class_name: str, method: str) -> str:
    """生成 Java_<pkg>_<Class>_<method>。"""
    parts = [jni_escape(part) for part in package.split(".")]
    parts.append(jni_escape(class_name))
    parts.append(jni_escape(method))
    return "Java_" + "_".join(parts)


def parse_kotlin_exports(kotlin_dir: Path) -> set[str]:
    """解析 Kotlin external fun，返回期望 JNI 符号集合。"""
    files = sorted(kotlin_dir.glob("*.kt"))
    if not files:
        raise GateError(f"Kotlin JNI dir missing/empty: {kotlin_dir}")
    expected: set[str] = set()
    for path in files:
        package = ""
        current_class: str | None = None
        for raw in path.read_text(encoding="utf-8").splitlines():
            line = raw.split("//", 1)[0]
            match_package = PACKAGE_RE.match(line)
            if match_package:
                package = match_package.group(1)
                continue
            match_class = CLASS_RE.match(line)
            if match_class:
                current_class = match_class.group(1)
                continue
            match_external = EXTERNAL_RE.match(line)
            if match_external and current_class in TARGET_CLASSES:
                if not package:
                    raise GateError(f"{path}: external fun without package")
                expected.add(
                    expected_symbol(package, current_class, match_external.group(1)),
                )
    if not expected:
        raise GateError("no expected JNI symbols parsed from Kotlin externals")
    return expected


def find_ndk(explicit: str | None) -> Path:
    """按显式参数/环境变量/local.properties/SDK 目录找 NDK。"""
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit))
    for variable in ("ANDROID_NDK_HOME", "ANDROID_NDK_ROOT", "NDK_HOME"):
        value = os.environ.get(variable)
        if value:
            candidates.append(Path(value))
    local_properties = REPO_ROOT / "local.properties"
    if local_properties.is_file():
        for line in local_properties.read_text(encoding="utf-8").splitlines():
            if line.startswith("ndk.dir="):
                candidates.append(Path(line.split("=", 1)[1].strip()))
    for sdk in (
        os.environ.get("ANDROID_HOME"),
        os.environ.get("ANDROID_SDK_ROOT"),
        str(Path.home() / "Library" / "Android" / "sdk"),
    ):
        if sdk:
            candidates.extend(Path(item) for item in glob.glob(str(Path(sdk) / "ndk" / "*")))
    for candidate in candidates:
        if (candidate / "toolchains" / "llvm" / "prebuilt").is_dir():
            return candidate
    raise GateError("NDK not found；设置 ANDROID_NDK_HOME/ANDROID_NDK_ROOT 或传 --ndk")


def find_nm(ndk: Path, explicit: str | None) -> Path:
    """找 NDK toolchain 内 llvm-nm。"""
    if explicit:
        return Path(explicit)
    prebuilt = ndk / "toolchains" / "llvm" / "prebuilt"
    for host in sorted(prebuilt.iterdir()):
        candidate = host / "bin" / "llvm-nm"
        if candidate.is_file():
            return candidate
    found = shutil.which("llvm-nm")
    if found:
        return Path(found)
    raise GateError(f"llvm-nm not found in NDK {ndk}")


def default_library(config: str, explicit: Path | None) -> Path:
    """默认取 Gradle cxx 中间产物的 libghostlock43499.so。"""
    if explicit:
        if not explicit.is_file():
            raise GateError(f"artifact missing: {explicit}")
        return explicit
    pattern = (
        REPO_ROOT / "build" / "app" / "intermediates" / "cxx" / config / "*" / "obj"
        / "arm64-v8a" / LIB_NAME
    )
    matches = sorted(Path(item) for item in glob.glob(str(pattern)))
    if not matches:
        raise GateError(f"{config} artifact missing: {LIB_NAME}; 先跑 externalNativeBuild{config}")
    return matches[-1]


def defined_java_symbols(nm: Path, library: Path) -> set[str]:
    """llvm-nm -D --defined-only 提取 Java_ 动态导出符号。"""
    completed = subprocess.run(
        [str(nm), "-D", "--defined-only", str(library)],
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        raise GateError(f"llvm-nm failed rc={completed.returncode}: {library}")
    return set(SYMBOL_RE.findall(completed.stdout))


def check_build(label: str, nm: Path, library: Path, expected: set[str]) -> list[str]:
    """断言 expected ⊆ defined；返回缺失符号列表。"""
    found = defined_java_symbols(nm, library)
    missing = sorted(expected - found)
    jni_classes = sorted({symbol.split("_")[-2] for symbol in expected})
    log(
        f"{label}: library={library} expected={len(expected)} found_java={len(found)} "
        f"missing={len(missing)}",
    )
    for class_name in jni_classes:
        cls_expected = {s for s in expected if s.split("_")[-2] == class_name}
        cls_found = {s for s in cls_expected if s in found}
        log(f"  {label} class={class_name} expected={len(cls_expected)} found={len(cls_found)}")
    if missing:
        for symbol in missing:
            log(f"FAIL {label} missing: {symbol}")
        return missing
    extra = sorted(found - expected)
    if extra:
        log(f"INFO {label} extra Java_ symbols (not in Kotlin externals): {len(extra)}")
        for symbol in extra:
            log(f"  EXTRA {label}: {symbol}")
    log(f"PASS {label} all expected JNI exports present")
    return []


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="verify JNI exports for libghostlock43499.so")
    parser.add_argument("--ndk", default=None)
    parser.add_argument("--nm", default=None)
    parser.add_argument("--kotlin-dir", type=Path, default=KOTLIN_DIR)
    parser.add_argument("--debug-lib", type=Path, default=None)
    parser.add_argument("--release-lib", type=Path, default=None)
    args = parser.parse_args(argv)
    try:
        expected = parse_kotlin_exports(args.kotlin_dir)
        ndk = find_ndk(args.ndk)
        nm = find_nm(ndk, args.nm)
        log(f"nm={nm}")
        log(f"expected_jni_symbols={len(expected)}")
        failures: list[str] = []
        for label, config, explicit in (
            ("Debug", "Debug", args.debug_lib),
            ("Release", "RelWithDebInfo", args.release_lib),
        ):
            library = default_library(config, explicit)
            failures.extend(check_build(label, nm, library, expected))
        if failures:
            log(f"verify_jni_exports: FAIL missing={len(failures)}")
            return 1
        log(f"verify_jni_exports: OK (Debug+Release expected={len(expected)})")
        return 0
    except GateError as problem:
        log(f"verify_jni_exports: ERROR {problem}")
        return 2


if __name__ == "__main__":
    sys.exit(main())
