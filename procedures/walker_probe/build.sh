#!/usr/bin/env bash
# walker_probe/build.sh - cross-compile glk_walker for aarch64 Android.
#
# Produces a minimal PIE executable whose DT_NEEDED is "libc++.so": it loads
# the SYSTEM C++ runtime (/system/lib64/libc++.so - the file whose page cache
# carries the sentry hook). The std::cout line in main.cpp is the trigger;
# see README.md for the hook site, the device facts and the protocol.
#
# Why the hand-declared ABI (main.cpp) and an explicit link input:
#   The NDK's libc++ and the platform libc++ are not symbol compatible: the
#   NDK headers emit std::__ndk1 (__config_site defines _LIBCPP_ABI_NAMESPACE
#   __ndk1) while /system/lib64/libc++.so exports std::__1, so a stock NDK
#   build can never bind to the system runtime. main.cpp therefore declares
#   the minimal std::__1 surface by hand (cout / put / flush / ios_base::Init)
#   and links against a link-time copy of the device's libc++.so; DT_NEEDED
#   stays "libc++.so" (that file's SONAME) and the device loader maps the
#   real, patched file at exec time. Never add -static-libstdc++ /
#   libc++_static: a static runtime never reaches the hooked code.
#
# Usage:
#   ./build.sh [OUT_DIR]
#   LIBCXX_SO=/path/to/libc++.so ./build.sh [OUT_DIR]
#
#   OUT_DIR    default: ${TMPDIR:-/tmp}/glk_walker_build (keeps the repo clean)
#   LIBCXX_SO  link-time input, default $OUT_DIR/libc++.so. One-time read-only
#              pull from a device carrying the same image:
#                adb -s <SERIAL> pull /system/lib64/libc++.so "$OUT_DIR/libc++.so"
#              The device resolves the runtime copy by SONAME; the local copy
#              only feeds the linker.
#
# NDK resolution order: local.properties ndk.dir -> $ANDROID_NDK_HOME ->
#   $ANDROID_NDK_ROOT -> newest ~/Library/Android/sdk/ndk/*.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$HERE/../.." && pwd)"
OUT_DIR="${1:-${TMPDIR:-/tmp}/glk_walker_build}"
LIBCXX_SO="${LIBCXX_SO:-$OUT_DIR/libc++.so}"

# --- resolve the NDK ----------------------------------------------------
NDK="${ANDROID_NDK_HOME:-}"
[ -n "$NDK" ] || NDK="${ANDROID_NDK_ROOT:-}"
if [ -z "$NDK" ] && [ -f "$REPO_ROOT/local.properties" ]; then
    NDK="$(sed -n 's/^ndk\.dir=//p' "$REPO_ROOT/local.properties" | head -n 1)"
fi
if [ -z "$NDK" ] || [ ! -d "$NDK" ]; then
    NDK="$(ls -d "$HOME"/Library/Android/sdk/ndk/* 2>/dev/null | sort | tail -n 1 || true)"
fi
if [ -z "$NDK" ] || [ ! -d "$NDK" ]; then
    echo "error: NDK not found; set ANDROID_NDK_HOME or local.properties ndk.dir" >&2
    exit 1
fi

PREBUILT="$NDK/toolchains/llvm/prebuilt"
HOST_TAG=""
for tag in darwin-x86_64 darwin-aarch64 linux-x86_64; do
    if [ -d "$PREBUILT/$tag" ]; then
        HOST_TAG="$tag"
        break
    fi
done
if [ -z "$HOST_TAG" ]; then
    echo "error: no prebuilt toolchain under $PREBUILT" >&2
    exit 1
fi
BIN="$PREBUILT/$HOST_TAG/bin"
CXX="$BIN/aarch64-linux-android30-clang++"
READELF="$BIN/llvm-readelf"
if [ ! -x "$CXX" ]; then
    echo "error: $CXX not found" >&2
    exit 1
fi

# --- the link-time libc++ input ----------------------------------------
if [ ! -f "$LIBCXX_SO" ]; then
    echo "error: link-time libc++.so not found: $LIBCXX_SO" >&2
    echo "pull it once (read-only) from the device:" >&2
    echo "  adb -s <SERIAL> pull /system/lib64/libc++.so \"$OUT_DIR/libc++.so\"" >&2
    exit 1
fi
if ! "$READELF" -d "$LIBCXX_SO" | grep -q 'Library soname: \[libc++.so\]'; then
    echo "error: $LIBCXX_SO is not the system libc++ (SONAME != libc++.so)" >&2
    echo "       a libc++_shared.so or static-libc++ build cannot step on the hook" >&2
    exit 1
fi

# --- build ---------------------------------------------------------------
mkdir -p "$OUT_DIR"
"$CXX" -O1 -fPIE -pie -nostdlib++ "$HERE/main.cpp" "$LIBCXX_SO" -o "$OUT_DIR/glk_walker"

# --- verify the linkage shape -------------------------------------------
echo "== DT_NEEDED =="
"$READELF" -d "$OUT_DIR/glk_walker" | grep NEEDED
if "$READELF" -d "$OUT_DIR/glk_walker" | grep -q 'libc++_shared'; then
    echo "error: libc++_shared.so in DT_NEEDED - the probe would not load the system libc++" >&2
    exit 1
fi
if ! "$READELF" -d "$OUT_DIR/glk_walker" | grep -q '\[libc++.so\]'; then
    echo "error: DT_NEEDED lacks libc++.so - std::cout would not resolve from /system/lib64" >&2
    exit 1
fi
NM="$BIN/llvm-nm"
if ! "$NM" -D "$OUT_DIR/glk_walker" | grep -q '_ZNSt3__14coutE'; then
    echo "error: std::cout is absent from .dynsym - libc++ looks static; the probe cannot step" >&2
    exit 1
fi
if "$NM" -D "$OUT_DIR/glk_walker" | grep -q 'St6__ndk1'; then
    echo "error: probe references std::__ndk1 names - NDK libc++ ABI leaked in;" >&2
    echo "       such a binary can never bind to the platform libc++ (see README)" >&2
    exit 1
fi
echo "== ok: $OUT_DIR/glk_walker =="
