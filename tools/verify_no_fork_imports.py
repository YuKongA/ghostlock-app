#!/usr/bin/env python3
"""零 fork ② 严格口径验证（task-60，无需设备）。

断言对象：
- **Release/RelWithDebInfo（生产构建，无 `--wrap`）** 的
  `libghostlock43499.so` 与 `libghostlock_victim_helper.so`：
  - 无 `fork` / `vfork` 符号（未定义导入或定义都不允许）；
  - 无 `__wrap_fork` / `__wrap_vfork` / `__real_fork` / `__real_vfork`；
  - `posix_spawn` 必须存在（生产 spawn 机制）；
  - `clone` 允许（由 bionic posix_spawn 内部使用，不要求符号出现）。
- **Debug（测量构建，CMake 传 `-Wl,--wrap=fork/vfork`）**：
  - 允许并期望 `fork`/`vfork` 未定义导入 + `__wrap_fork`/`__wrap_vfork` 定义；
  - 仅作 E4 ① 计数测量，不参与严格 ② 判据。

用法（host，无需设备）：
    ./gradlew :app:externalNativeBuildRelease :app:externalNativeBuildDebug
    python3 tools/verify_no_fork_imports.py

可选参数：
    --release-dir <dir> / --debug-dir <dir>  覆盖默认 Gradle 产物目录；
    --ndk <ndk-dir> / --readelf <path> / --nm <path>
默认从 `ANDROID_NDK_HOME`/`ANDROID_NDK_ROOT`/`local.properties`/SDK 目录找 NDK。

退出码：0=PASS；1=断言失败；2=环境/产物缺失（先构建）。
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
LIB_43499 = "libghostlock43499.so"
LIB_HELPER = "libghostlock_victim_helper.so"
FORBIDDEN_RELEASE_SYMBOLS = {"fork", "vfork"}
WRAP_SYMBOLS = {"__wrap_fork", "__wrap_vfork", "__real_fork", "__real_vfork"}
REQUIRED_RELEASE_SYMBOLS = {"posix_spawn"}


class VerificationError(RuntimeError):
    """验证失败（断言/环境）。"""


def log(message: str) -> None:
    print(message, flush=True)


def find_ndk(explicit: str | None) -> Path:
    """按显式参数、环境变量、local.properties、SDK 目录顺序找 NDK。"""
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
    raise VerificationError(
        "NDK not found；设置 ANDROID_NDK_HOME/ANDROID_NDK_ROOT 或传 --ndk",
    )


def find_host_tool(ndk: Path, tool: str, explicit: str | None) -> Path:
    """找 NDK toolchain 内的 llvm-readelf/llvm-nm。"""
    if explicit:
        return Path(explicit)
    prebuilt = ndk / "toolchains" / "llvm" / "prebuilt"
    for host in sorted(prebuilt.iterdir()):
        candidate = host / "bin" / tool
        if candidate.is_file():
            return candidate
    found = shutil.which(tool)
    if found:
        return Path(found)
    raise VerificationError(f"{tool} not found in NDK {ndk}")


def default_release_dir() -> Path:
    """默认 Gradle Release/RelWithDebInfo C/C++ 产物目录（arm64-v8a）。"""
    pattern = REPO_ROOT / "build" / "app" / "intermediates" / "cxx" / "RelWithDebInfo" / "*" / "obj" / "arm64-v8a"
    matches = sorted(Path(item) for item in glob.glob(str(pattern)))
    if not matches:
        raise VerificationError(
            "Release native artifacts missing；先跑 :app:externalNativeBuildRelease",
        )
    return matches[-1]


def default_debug_dir() -> Path:
    """默认 Gradle Debug C/C++ 产物目录（arm64-v8a）。"""
    pattern = REPO_ROOT / "build" / "app" / "intermediates" / "cxx" / "Debug" / "*" / "obj" / "arm64-v8a"
    matches = sorted(Path(item) for item in glob.glob(str(pattern)))
    if not matches:
        raise VerificationError(
            "Debug native artifacts missing；先跑 :app:externalNativeBuildDebug",
        )
    return matches[-1]


def read_symbols(readelf: Path, library: Path) -> dict[str, set[str]]:
    """返回 {name: {kind}}；kind=`undef`/`defined`（readelf -Ws）。"""
    completed = subprocess.run(
        [str(readelf), "-Ws", str(library)],
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        raise VerificationError(f"readelf failed rc={completed.returncode}: {library}")
    symbols: dict[str, set[str]] = {}
    for line in completed.stdout.splitlines():
        stripped = line.strip()
        if not stripped or not re.match(r"^\d+:", stripped):
            continue
        fields = stripped.split()
        if len(fields) < 8:
            continue
        name = fields[7].split("@", 1)[0]
        kind = "undef" if fields[6] == "UND" else "defined"
        symbols.setdefault(name, set()).add(kind)
    return symbols


def read_dynamic_undefined(nm: Path, library: Path) -> set[str]:
    """readelf 的 nm -D 交叉复核：返回未定义动态符号名。"""
    completed = subprocess.run(
        [str(nm), "-D", "--undefined-only", str(library)],
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        raise VerificationError(f"nm failed rc={completed.returncode}: {library}")
    names: set[str] = set()
    for line in completed.stdout.splitlines():
        fields = line.split()
        if len(fields) >= 2 and fields[0] in {"U", "w", "v"}:
            names.add(fields[1].split("@", 1)[0])
    return names


def check_release(readelf: Path, nm: Path, library: Path) -> list[str]:
    """严格 ②：Release 产物无 fork/vfork/wrap 依赖，posix_spawn 存在。"""
    if not library.is_file():
        raise VerificationError(f"missing release artifact: {library}")
    symbols = read_symbols(readelf, library)
    errors: list[str] = []
    for name in sorted(FORBIDDEN_RELEASE_SYMBOLS):
        if name in symbols:
            errors.append(f"{library.name}: forbidden symbol {name} ({sorted(symbols[name])})")
    for name in sorted(WRAP_SYMBOLS):
        if name in symbols:
            errors.append(f"{library.name}: wrap symbol must be absent in Release: {name}")
    for name in sorted(REQUIRED_RELEASE_SYMBOLS):
        if name not in symbols:
            errors.append(f"{library.name}: required production symbol missing: {name}")
    undefined = read_dynamic_undefined(nm, library)
    leaked = sorted(undefined & (FORBIDDEN_RELEASE_SYMBOLS | WRAP_SYMBOLS))
    if leaked:
        errors.append(f"{library.name}: nm -D undefined leaked: {leaked}")
    spawn = "UND" if "undef" in symbols.get("posix_spawn", set()) else "defined"
    if errors:
        for error in errors:
            log(f"FAIL {error}")
        return errors
    log(
        f"PASS release {library.name}: fork=absent vfork=absent "
        f"wrap=absent posix_spawn={spawn} nm_undefined_ok",
    )
    return []


def check_debug(readelf: Path, library: Path) -> list[str]:
    """Debug 测量口径：wrap 别名必须存在（允许 fork/vfork 导入）。"""
    if not library.is_file():
        raise VerificationError(f"missing debug artifact: {library}")
    symbols = read_symbols(readelf, library)
    errors: list[str] = []
    for name in ("__wrap_fork", "__wrap_vfork"):
        if name not in symbols:
            errors.append(f"{library.name}: debug wrap symbol missing: {name}")
    for name in ("fork", "vfork"):
        if "undef" not in symbols.get(name, set()):
            errors.append(f"{library.name}: debug expected fork/vfork undefined import: {name}")
    if errors:
        for error in errors:
            log(f"FAIL {error}")
        return errors
    log(
        f"INFO debug {library.name}: fork/vfork import + __wrap_* present "
        f"(E4 ① measurement only; not strict ② evidence)",
    )
    return []


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="verify zero-fork ② strict Release symbols")
    parser.add_argument("--release-dir", type=Path, default=None)
    parser.add_argument("--debug-dir", type=Path, default=None)
    parser.add_argument("--ndk", default=None)
    parser.add_argument("--readelf", default=None)
    parser.add_argument("--nm", default=None)
    args = parser.parse_args(argv)

    try:
        ndk = find_ndk(args.ndk)
        readelf = find_host_tool(ndk, "llvm-readelf", args.readelf)
        nm = find_host_tool(ndk, "llvm-nm", args.nm)
        release_dir = args.release_dir or default_release_dir()
        debug_dir = args.debug_dir or default_debug_dir()
        log(f"readelf={readelf}")
        log(f"release_dir={release_dir}")
        log(f"debug_dir={debug_dir}")
        errors: list[str] = []
        for library in (LIB_43499, LIB_HELPER):
            errors.extend(check_release(readelf, nm, release_dir / library))
        # Debug 证据：只报 INFO/FAIL（FAIL 仅表示 Debug wrap 缺失，非 Release 判据）。
        for library in (LIB_43499, LIB_HELPER):
            errors.extend(check_debug(readelf, debug_dir / library))
        if errors:
            log(f"verify_no_fork_imports: FAIL count={len(errors)}")
            return 1
        log("verify_no_fork_imports: OK (Release=no fork/vfork imports; posix_spawn present)")
        return 0
    except VerificationError as problem:
        log(f"verify_no_fork_imports: ERROR {problem}")
        return 2


if __name__ == "__main__":
    sys.exit(main())
