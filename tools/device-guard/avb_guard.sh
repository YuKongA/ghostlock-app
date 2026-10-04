#!/usr/bin/env bash
# GhostLock AVB guard: baseline / check / alert / repair for AVB-verified content.
#
# Usage:
#   avb_guard.sh baseline <serial> <outdir> [--baseline-out FILE] [--skip LIST]
#                                   [--avbtool PATH] [--dry-run] [--image-dir DIR]
#   avb_guard.sh check    <serial> <baseline.tsv> [--report FILE] [--fail-list FILE]
#                                   [--image-dir DIR] [--skip LIST] [--dry-run]
#   avb_guard.sh alert    <message> [--outdir DIR] [--no-notify] [--no-dialog]
#                                   [--no-say] [--force-dialog] [--require-delivery]
#   avb_guard.sh repair   <serial> <outdir> <baseline.tsv> --yes [--allow-super-restore]
#                                   [--max-rounds N] [--cold-reboot] [--dry-run]
#                                   [--image-dir DIR] [--skip LIST] [--report FILE]
#   avb_guard.sh files-baseline <serial> <outdir> [--baseline-out FILE] [--extra PATH]...
#                                   [--vendor-scan] [--vendor-glob PAT] [--vendor-max N]
#                                   [--report FILE] [--dry-run --local-root DIR]
#   avb_guard.sh files-check    <serial> <files-baseline.tsv> [--report FILE]
#                                   [--fail-list FILE] [--strict-errors]
#                                   [--dry-run --local-root DIR]
#   avb_guard.sh verity-status  <serial> [--report FILE] [--outdir DIR]
#                                   [--no-alert] [--dry-run]
#   avb_guard.sh avb-verify     <outdir> [--slot SLOT] [--report FILE] [--verify]
#   avb_guard.sh check-all      <serial> <outdir> [--partition-baseline FILE]
#                                   [--files-baseline FILE] [--image-dir DIR]
#                                   [--report FILE] [--slot SLOT] [--local-root DIR]
#                                   [--dry-run] [--skip-partition] [--skip-files]
#                                   [--skip-verity] [--skip-avb]
#
# Companion doc: docs/analysis/device-gates/avb-guard-workflow.md
#
# Exit codes:
#   0  consistent / success
#   1  inconsistent, or unrecoverable error, or repair exhausted
#   2  usage error
#   3  --require-delivery set and no interactive alert channel was delivered
#
# Safety:
#   * check is read-only.
#   * All destructive operations (repair writes) require --yes.
#   * metadata is NEVER restored. super restore additionally requires
#     --allow-super-restore (running-device write is high risk; prefer fastboot).
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
# Absolute path to this script, used by check-all to invoke sibling subcommands.
GUARD_SELF="$SCRIPT_DIR/avb_guard.sh"
ADB_BIN=${ADB_BIN:-/Users/nickji/Library/Android/sdk/platform-tools/adb}
SERIAL=""
TS=$(date -u +%Y%m%dT%H%M%SZ)

# Single-quoted "$ADB" containing "adb -s SERIAL" cannot be invoked as a command,
# so device calls go through this function with the serial passed via $SERIAL.
adb_run() { "$ADB_BIN" -s "$SERIAL" "$@"; }

# ---- partition inventory ---------------------------------------------------
# Physical partitions carrying AVB-verified boot / vbmeta content (both slots).
AVB_PHYS="super vbmeta_a vbmeta_b vbmeta_system_a vbmeta_system_b \
boot_a boot_b init_boot_a init_boot_b dtbo_a dtbo_b \
vendor_boot_a vendor_boot_b recovery_a recovery_b vm-bootsys_a vm-bootsys_b"
# Read-only dm-verity views = what AVB/devicetree verifies at boot.
VERITY_VIEWS="system-verity system_ext-verity product-verity vendor-verity \
odm-verity system_dlkm-verity vendor_dlkm-verity"
# Writable state partitions: expected to differ across boots; reported separately.
STATE_PARTS="metadata misc"

BASELINE_DIR_DEFAULT="$SCRIPT_DIR/baselines"
# Durable, repo-external backup root: must NOT live under build/ (gradle clean wipes it).
BACKUP_ROOT_DEFAULT="${GHOSTLOCK_BACKUP_DIR:-/Users/nickji/Documents/Program/Analysis/ghostlock-device-backup}"
REPORT_DIR_DEFAULT="$REPO_ROOT/build/device-backup/avb-guard-reports"
ALERT_DIR_DEFAULT="$REPO_ROOT/build/device-backup/avb-guard-alerts"
GUARD_LOG_DEFAULT="$REPO_ROOT/build/device-backup/avb-guard.log"
GUARD_LOG=${GUARD_LOG:-$GUARD_LOG_DEFAULT}
# sha256 of the empty byte stream (used to detect an unreadable/absent node).
EMPTY_SHA256=e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855

usage() {
  # Print every leading comment line (skipping the shebang) up to the
  # "Companion doc" marker; awk is portable across GNU and BSD userlands.
  awk 'NR > 1 && /^# Companion doc/ { sub(/^# ?/, ""); print; exit }
       NR > 1 { sub(/^# ?/, ""); print }' "$0"
}

now_iso() { date -u +%Y-%m-%dT%H:%M:%SZ; }

log() { printf '[avb-guard] %s\n' "$*" >&2; }

log_persist() {
  mkdir -p "$(dirname "$GUARD_LOG")" 2>/dev/null || true
  printf '%s %s\n' "$(now_iso)" "$*" >>"$GUARD_LOG" 2>/dev/null || true
}

die() { log "ERROR: $*"; exit 1; }

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

hash_file() { shasum -a 256 "$1" | awk '{print $1}'; }

file_size() {
  stat -f%z "$1" 2>/dev/null || stat -c%s "$1"
}

osa_escape() { printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g'; }

# ---- device helpers --------------------------------------------------------
prop() {
  adb_run shell getprop "$1" </dev/null 2>/dev/null | tr -d '\r' | head -1
}

dev_for() {
  local n=$1
  if adb_run shell "su -c 'test -e /dev/block/by-name/$n'" </dev/null >/dev/null 2>&1; then
    printf '/dev/block/by-name/%s' "$n"
  else
    printf '/dev/block/mapper/%s' "$n"
  fi
}

dev_exists() {
  local n=$1 d
  d=$(dev_for "$n")
  adb_run shell "su -c 'test -e $d'" </dev/null >/dev/null 2>&1
}

device_size() {
  local n=$1 d
  d=$(dev_for "$n")
  adb_run shell "su -c 'blockdev --getsize64 $d'" </dev/null 2>/dev/null \
    | tr -d '\r' | tr -dc '0-9'
}

# Stream a partition through sha256; echo the hex digest. Returns non-zero on
# adb error. A temp file captures the digest so PIPESTATUS stays observable
# (it is not visible across a $(...) command substitution).
device_hash() {
  local n=$1 d tmph rc h
  d=$(dev_for "$n")
  tmph=$(mktemp "${TMPDIR:-/tmp}/avbhash.XXXXXX")
  set +e
  adb_run exec-out "su -c 'dd if=$d bs=4M 2>/dev/null'" </dev/null 2>/dev/null \
    | shasum -a 256 | awk '{print $1}' >"$tmph"
  rc=${PIPESTATUS[0]}
  set -e
  h=$(cat "$tmph")
  rm -f "$tmph"
  # adb can return 0 for a missing device node while emitting nothing; reject the
  # SHA-256 of the empty stream so an absent partition is [ERR], not [FAIL].
  if [ "$rc" -ne 0 ] || [ -z "$h" ] || [ "$h" = "$EMPTY_SHA256" ]; then
    return 1
  fi
  printf '%s' "$h"
}

dump_image() {
  local n=$1 dest=$2 d sz sha
  d=$(dev_for "$n")
  adb_run exec-out "su -c 'dd if=$d bs=4M 2>/dev/null'" </dev/null >"$dest"
  sz=$(file_size "$dest")
  sha=$(hash_file "$dest")
  printf '%s\t%s' "$sz" "$sha"
}

# ---- baseline --------------------------------------------------------------
baseline_image_dir() { sed -n 's/^# image_dir=//p' "$1" | head -1; }

detect_avbtool() {
  if [ -n "${AVBTOOL:-}" ] && [ -f "${AVBTOOL:-}" ]; then printf '%s' "${AVBTOOL:-}"; return; fi
  if command -v avbtool >/dev/null 2>&1; then command -v avbtool; return; fi
  if python3 -c 'import avbtool' >/dev/null 2>&1; then printf 'python3 -m avbtool'; return; fi
  printf ''
}

run_avbtool() {
  local tool=$1 img=$2 out=$3
  [ -n "$tool" ] || return 0
  {
    printf '### avbtool info_image %s\n' "$img"
    if eval "$tool info_image --image '$img'" 2>&1; then
      printf '### avbtool info_image rc=0\n'
    else
      printf '### avbtool info_image rc=non-zero\n'
    fi
    printf '### avbtool verify_image %s\n' "$img"
    if eval "$tool verify_image --image '$img'" 2>&1; then
      printf '### avbtool verify_image rc=0\n'
    else
      printf '### avbtool verify_image rc=non-zero\n'
    fi
  } >>"$out"
}

cmd_baseline() {
  [ $# -ge 2 ] || { usage; exit 2; }
  local serial=$1 outdir=$2; shift 2
  local baseline_out="" skip=${SKIP_ENTRIES:-} avbtool=${AVBTOOL:-}
  [ -n "$outdir" ] || outdir="$BACKUP_ROOT_DEFAULT/$serial-$(date -u +%Y%m%d)"
  local dry_run=0 image_dir=""
  while [ $# -gt 0 ]; do
    case "$1" in
      --baseline-out) baseline_out=$2; shift ;;
      --image-dir) image_dir=$2; shift ;;
      --skip) skip="$skip $2"; shift ;;
      --avbtool) avbtool=$2; shift ;;
      --dry-run) dry_run=1 ;;
      -h|--help) usage; exit 0 ;;
      *) log "unknown option: $1"; exit 2 ;;
    esac
    shift
  done

  [ -n "$baseline_out" ] || baseline_out="$BASELINE_DIR_DEFAULT/$serial-avb-baseline.tsv"
  [ -n "$image_dir" ] || image_dir="$outdir"
  mkdir -p "$outdir" "$(dirname "$baseline_out")"

  if [ "$dry_run" -ne 1 ]; then
    require_cmd "$ADB_BIN"
    SERIAL=$serial
    adb_run get-state </dev/null >/dev/null 2>&1 || die "device $serial not available via $ADB_BIN"
  fi

  local tool
  if [ -n "$avbtool" ]; then tool=$avbtool; else tool=$(detect_avbtool); fi

  local tmp="$outdir/avb-baseline.tsv.part"
  {
    printf '# ghostlock avb_guard baseline v1 (TSV, columns below)\n'
    printf '# serial=%s\n' "$serial"
    printf '# date=%s\n' "$(now_iso)"
    printf '# image_dir=%s\n' "$image_dir"
    printf '# columns=name\tclass\tsize\tsha256\tsource\n'
    if [ "$dry_run" -eq 1 ]; then
      printf '# mode=dry-run (hashed local images, no device contact)\n'
      printf '# device=LOCAL\n'
      printf '# avbtool=%s\n' "${tool:-absent (not used)}"
    else
      printf '# device=%s\n' "$(prop ro.product.device)"
      printf '# model=%s\n' "$(prop ro.product.model)"
      printf '# release=%s\n' "$(prop ro.build.version.release)"
      printf '# fingerprint=%s\n' "$(prop ro.build.fingerprint)"
      printf '# verifiedbootstate=%s\n' "$(prop ro.boot.verifiedbootstate)"
      printf '# vbmeta_device_state=%s\n' "$(prop ro.boot.vbmeta.device_state)"
      printf '# slot=%s\n' "$(prop ro.boot.slot_suffix)"
      printf '# avb_version=%s\n' "$(prop ro.boot.avb_version)"
      printf '# avbtool=%s\n' "${tool:-absent (not used)}"
    fi
  } >"$tmp"

  local total=0 absents=0
  local list="$AVB_PHYS $VERITY_VIEWS $STATE_PARTS"
  local n class
  for n in $list; do
    case " $skip " in *" $n "*) log "[skip] $n (--skip/SKIP_ENTRIES)"; continue ;; esac
    case " $STATE_PARTS " in *" $n "*) class=state ;; *) class=avb ;; esac
    case " $VERITY_VIEWS " in *" $n "*) class=verity ;; esac

    local img="$image_dir/$n.img"
    local sz sha
    if [ "$dry_run" -eq 1 ]; then
      if [ ! -f "$img" ]; then
        printf '# absent: %s (no local image %s)\n' "$n" "$img" >>"$tmp"
        absents=$((absents + 1))
        continue
      fi
      sz=$(file_size "$img"); sha=$(hash_file "$img")
    else
      if ! dev_exists "$n"; then
        printf '# absent: %s (device node missing)\n' "$n" >>"$tmp"
        log "[skip] $n (absent)"
        absents=$((absents + 1))
        continue
      fi
      local pair
      pair=$(dump_image "$n" "$img")
      sz=${pair%%$'\t'*}; sha=${pair##*$'\t'}
      case "$n" in
        vbmeta*|vm-bootsys*) run_avbtool "$tool" "$img" "$outdir/avbtool.txt" ;;
      esac
    fi

    local src
    if [ "$dry_run" -eq 1 ]; then src="local:$img"; else src="device:$serial:$(dev_for "$n")"; fi
    printf '%s\t%s\t%s\t%s\t%s\n' "$n" "$class" "$sz" "$sha" "$src" >>"$tmp"
    printf '[baseline] %-22s class=%-6s size=%-12s sha256=%s\n' "$n" "$class" "$sz" "$sha"
    total=$((total + 1))
  done

  cp "$tmp" "$baseline_out"
  cp "$tmp" "$outdir/avb-baseline.tsv" 2>/dev/null || true
  rm -f "$tmp"
  if [ -n "$tool" ]; then
    cp "$outdir/avbtool.txt" "$(dirname "$baseline_out")/$serial-avbtool.txt" 2>/dev/null || true
  fi
  log "baseline written: $baseline_out ($total entries, $absents absent); images -> $image_dir"
  printf 'BASELINE_FILE=%s\n' "$baseline_out"
  printf 'IMAGE_DIR=%s\n' "$image_dir"
  [ "$total" -gt 0 ] || die "no entries captured"
}

# ---- check -----------------------------------------------------------------
G_SERIAL=""; G_BASELINE=""; G_REPORT=""; G_FAILLIST=""; G_DRY_RUN=0
G_IMAGE_DIR=""; G_SKIP=""

do_check() {
  : >"$G_REPORT"
  : >"$G_FAILLIST"

  report() { printf '%s\n' "$*" | tee -a "$G_REPORT"; }

  report "# ghostlock avb_guard check"
  report "# serial=$G_SERIAL baseline=$G_BASELINE date=$(now_iso) dry_run=$G_DRY_RUN"
  if [ "$G_DRY_RUN" -eq 1 ]; then
    report "# mode=dry-run image_dir=$G_IMAGE_DIR"
  else
    report "# device=$(prop ro.product.device) release=$(prop ro.build.version.release)"
    report "# verifiedbootstate=$(prop ro.boot.verifiedbootstate) vbmeta_device_state=$(prop ro.boot.vbmeta.device_state) slot=$(prop ro.boot.slot_suffix)"
  fi
  report "#"

  local fails=0 errs=0 vars=0 oks=0 total=0
  local name class size sha src
  exec 3<"$G_BASELINE"
  while IFS=$'\t' read -r name class size sha src <&3; do
    case "$name" in ''|\#*) continue ;; esac
    case " $G_SKIP " in *" $name "*) report "[skip] $name (--skip)"; continue ;; esac
    total=$((total + 1))

    local got=""
    local is_state=0
    [ "$class" = state ] && is_state=1

    if [ "$G_DRY_RUN" -eq 1 ]; then
      local img="$G_IMAGE_DIR/$name.img"
      if [ ! -f "$img" ]; then
        report "[ERR]  $name ($class) missing local image $img"
        errs=$((errs + 1)); continue
      fi
      got=$(hash_file "$img")
    else
      if ! got=$(device_hash "$name"); then
        report "[ERR]  $name ($class) device read failed (root? device node?)"
        errs=$((errs + 1)); continue
      fi
    fi

    if [ "$got" = "$sha" ]; then
      report "[ok]   $name ($class)"
      oks=$((oks + 1))
    elif [ "$is_state" -eq 1 ]; then
      report "[var]  $name ($class) expected-variable expected=$sha got=$got"
      vars=$((vars + 1))
    else
      report "[FAIL] $name ($class) expected=$sha got=$got"
      fails=$((fails + 1))
      printf '%s\n' "$name" >>"$G_FAILLIST"
    fi
  done
  exec 3<&-

  report "#"
  report "# summary total=$total ok=$oks fail=$fails err=$errs expected_variable=$vars"
  if [ "$fails" -eq 0 ] && [ "$errs" -eq 0 ]; then
    report "# RESULT=CONSISTENT"
    return 0
  fi
  report "# RESULT=INCONSISTENT"
  return 1
}

cmd_check() {
  [ $# -ge 2 ] || { usage; exit 2; }
  G_SERIAL=$1; G_BASELINE=$2; shift 2
  local report="" faillist="" skip=${SKIP_ENTRIES:-}
  G_DRY_RUN=0; G_IMAGE_DIR=""
  while [ $# -gt 0 ]; do
    case "$1" in
      --dry-run) G_DRY_RUN=1 ;;
      --image-dir) G_IMAGE_DIR=$2; shift ;;
      --report) report=$2; shift ;;
      --fail-list) faillist=$2; shift ;;
      --skip) skip="$skip $2"; shift ;;
      -h|--help) usage; exit 0 ;;
      *) log "unknown option: $1"; exit 2 ;;
    esac
    shift
  done
  [ -f "$G_BASELINE" ] || die "baseline not found: $G_BASELINE"

  if [ "$G_DRY_RUN" -ne 1 ]; then
    require_cmd "$ADB_BIN"
    SERIAL=$G_SERIAL
    adb_run get-state </dev/null >/dev/null 2>&1 || die "device $G_SERIAL not available"
  fi

  if [ -z "$G_IMAGE_DIR" ]; then
    G_IMAGE_DIR=$(baseline_image_dir "$G_BASELINE")
  fi
  if [ "$G_DRY_RUN" -eq 1 ] && [ -z "$G_IMAGE_DIR" ]; then
    die "--dry-run needs --image-dir (or an '# image_dir=' header in the baseline)"
  fi

  [ -n "$report" ] || report="$REPORT_DIR_DEFAULT/check-$G_SERIAL-$TS.log"
  G_REPORT=$report
  mkdir -p "$(dirname "$G_REPORT")"
  [ -n "$faillist" ] || faillist="$G_REPORT.fail"
  G_FAILLIST=$faillist
  G_SKIP=$skip

  set +e
  do_check
  local rc=$?
  set -e
  printf 'REPORT_FILE=%s\n' "$G_REPORT"
  printf 'FAIL_LIST=%s\n' "$G_FAILLIST"
  if [ "$rc" -eq 0 ]; then
    log "check CONSISTENT -> $G_REPORT"
  else
    log "check INCONSISTENT for serial $G_SERIAL -> $G_REPORT"
  fi
  log_persist "check serial=$G_SERIAL baseline=$G_BASELINE rc=$rc report=$G_REPORT"
  exit "$rc"
}

# ---- alert -----------------------------------------------------------------
do_alert() {
  local msg=$1
  local dir=${ALERT_DIR:-$ALERT_DIR_DEFAULT}
  mkdir -p "$dir"
  local ts file
  ts=$(date -u +%Y%m%dT%H%M%SZ)
  file="$dir/ALERT-$ts.txt"
  {
    printf 'GhostLock AVB Guard alert\n'
    printf 'time=%s\n' "$(now_iso)"
    printf 'host=%s user=%s\n' "$(hostname)" "${USER:-unknown}"
    printf 'message=%s\n' "$msg"
  } >"$file"
  printf '%s message=%s\n' "$(now_iso)" "$msg" >>"$dir/alerts.log"
  log_persist "ALERT file=$file message=$msg"

  local interactive=0
  if [ -t 0 ] && [ -t 1 ] && [ -z "${AVB_GUARD_NONINTERACTIVE:-}" ]; then
    interactive=1
  fi

  local msg_esc; msg_esc=$(osa_escape "$msg")
  local gui=0

  if [ "${ALERT_NO_NOTIFY:-0}" -ne 1 ] && command -v osascript >/dev/null 2>&1; then
    if osascript -e "display notification \"$msg_esc\" with title \"GhostLock AVB Guard\"" >/dev/null 2>&1; then
      gui=1
    fi
  fi

  if { [ "$interactive" -eq 1 ] || [ "${ALERT_FORCE_DIALOG:-0}" -eq 1 ]; } \
     && [ "${ALERT_NO_DIALOG:-0}" -ne 1 ] && command -v osascript >/dev/null 2>&1; then
    log "showing blocking dialog (click 已了解 to continue)"
    if osascript -e "display dialog \"$msg_esc\" buttons {\"已了解\"} default button 1 with title \"GhostLock AVB Guard\" with icon caution" >/dev/null 2>&1; then
      gui=1
    fi
  fi

  if { [ "$interactive" -eq 1 ] || [ "${ALERT_FORCE_DIALOG:-0}" -eq 1 ]; } \
     && [ "${ALERT_NO_SAY:-0}" -ne 1 ] && command -v say >/dev/null 2>&1; then
    say "GhostLock AVB guard alert. $msg" >/dev/null 2>&1 || true
  fi

  printf 'ALERT_FILE=%s\n' "$file"
  printf 'AVB_ALERT status=written file=%s gui=%s interactive=%s\n' "$file" "$gui" "$interactive"
  if [ "${ALERT_REQUIRE_DELIVERY:-0}" -eq 1 ] && [ "$gui" -ne 1 ]; then
    log "alert delivery required but no interactive channel was delivered"
    return 3
  fi
  return 0
}

cmd_alert() {
  [ $# -ge 1 ] || { usage; exit 2; }
  local msg=$1; shift
  ALERT_DIR=${ALERT_DIR:-$ALERT_DIR_DEFAULT}
  while [ $# -gt 0 ]; do
    case "$1" in
      --outdir) ALERT_DIR=$2; shift ;;
      --no-notify) ALERT_NO_NOTIFY=1 ;;
      --no-dialog) ALERT_NO_DIALOG=1 ;;
      --no-say) ALERT_NO_SAY=1 ;;
      --force-dialog) ALERT_FORCE_DIALOG=1 ;;
      --require-delivery) ALERT_REQUIRE_DELIVERY=1 ;;
      -h|--help) usage; exit 0 ;;
      *) log "unknown option: $1"; exit 2 ;;
    esac
    shift
  done
  do_alert "$msg"
}

# ---- repair ----------------------------------------------------------------
device_sync() {
  adb_run shell "su -c sync" </dev/null >/dev/null 2>&1 || log "sync failed (continuing)"
}

drop_page_cache() {
  if [ -n "${AVB_FADVISE_CMD:-}" ]; then
    log "running AVB_FADVISE_CMD"
    eval "$AVB_FADVISE_CMD" || log "AVB_FADVISE_CMD failed (continuing)"
    return 0
  fi
  if adb_run shell "su -c 'echo 3 > /proc/sys/vm/drop_caches'" </dev/null >/dev/null 2>&1; then
    log "page cache dropped via /proc/sys/vm/drop_caches"
  else
    log "WARN: could not drop page cache (set AVB_FADVISE_CMD)"
  fi
}

wait_for_boot() {
  local i=0
  log "waiting for device $G_SERIAL to boot..."
  while [ "$i" -lt 120 ]; do
    i=$((i + 1))
    if [ "$(adb_run shell getprop sys.boot_completed </dev/null 2>/dev/null | tr -d '\r')" = "1" ]; then
      log "boot_completed after $i polls"
      sleep 2
      return 0
    fi
    sleep 2
  done
  log "WARN: boot_completed not observed within timeout"
  return 1
}

cold_reboot() {
  if [ -n "${AVB_COLD_REBOOT_CMD:-}" ]; then
    log "running AVB_COLD_REBOOT_CMD"
    eval "$AVB_COLD_REBOOT_CMD" || die "AVB_COLD_REBOOT_CMD failed"
  else
    log "issuing 'adb reboot' (warm reset; set AVB_COLD_REBOOT_CMD for a true power cycle)"
    adb_run reboot || die "adb reboot failed"
  fi
  adb_run wait-for-device </dev/null 2>/dev/null || true
  wait_for_boot || true
  regain_root_if_possible
}

regain_root_if_possible() {
  if adb_run shell "su -c 'id -u'" </dev/null 2>/dev/null | tr -d '\r' | grep -qx 0; then
    log "root available after reboot"
    return 0
  fi
  if [ -n "${AVB_REGAIN_ROOT_CMD:-}" ]; then
    log "re-acquiring root via AVB_REGAIN_ROOT_CMD"
    eval "$AVB_REGAIN_ROOT_CMD" || log "AVB_REGAIN_ROOT_CMD failed"
  else
    log "WARN: root (su) unavailable after reboot; re-check will report [ERR]."
    log "WARN: set AVB_REGAIN_ROOT_CMD (e.g. re-run the GLKv3 path) for repair after cold reboot."
  fi
}

super_restore_warning() {
  cat >&2 <<'SUPERWARN'
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
[WARN] super restore on a RUNNING device is HIGH RISK:
  - super backs the dm-linear mappings for system/vendor/product/...;
    rewriting it live can corrupt mounted logical partitions, crash, or brick;
  - metadata is a state partition and is NEVER restored;
  - prefer the fastboot path:
        adb reboot bootloader
        fastboot flash super <outdir>/super.img
        fastboot reboot
  - pass --allow-super-restore only if you explicitly accept the risk.
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
SUPERWARN
}

# Restore one partition image. Returns 0 ok, 1 failure, 2 refused.
restore_one() {
  local name=$1
  local img="$G_IMAGE_DIR/$name.img"
  if [ "$name" = "metadata" ]; then
    log "[refuse] metadata is a state partition and must not be overwritten"
    return 2
  fi
  if [ "$name" = "super" ] && [ "${ALLOW_SUPER_RESTORE:-0}" -ne 1 ]; then
    super_restore_warning
    log "[refuse] super restore requires --allow-super-restore"
    return 2
  fi
  if [ ! -f "$img" ]; then
    log "[ERR] missing backup image: $img"
    return 1
  fi
  if [ "$G_DRY_RUN" -eq 1 ]; then
    log "[dry-run] would write $img -> device:$name (no device contact)"
    return 0
  fi
  local dev; dev=$(dev_for "$name")
  log "restoring $name from $img -> $dev"
  if adb_run exec-in "su -c 'dd of=$dev bs=4M 2>/dev/null'" <"$img"; then
    return 0
  fi
  log "[ERR] restore failed for $name"
  return 1
}

repair_apply_backups() {
  local rc=0 name
  if [ ! -s "$G_FAILLIST" ]; then
    log "no failing partitions recorded; nothing to restore"
    return 0
  fi
  while IFS= read -r name; do
    [ -n "$name" ] || continue
    case " $STATE_PARTS " in *" $name "*)
      log "[skip] $name is a state partition"; continue ;;
    esac
    restore_one "$name" || rc=1
  done <"$G_FAILLIST"
  return "$rc"
}

cmd_repair() {
  [ $# -ge 3 ] || { usage; exit 2; }
  G_SERIAL=$1; local outdir=$2; G_BASELINE=$3; shift 3
  local yes=0 MAX_ROUNDS=3 cold=0 report="" skip=${SKIP_ENTRIES:-}
  G_DRY_RUN=0; G_IMAGE_DIR=""; ALLOW_SUPER_RESTORE=0
  while [ $# -gt 0 ]; do
    case "$1" in
      --yes|-y) yes=1 ;;
      --allow-super-restore) ALLOW_SUPER_RESTORE=1 ;;
      --max-rounds) MAX_ROUNDS=$2; shift ;;
      --cold-reboot) cold=1 ;;
      --dry-run) G_DRY_RUN=1 ;;
      --image-dir) G_IMAGE_DIR=$2; shift ;;
      --report) report=$2; shift ;;
      --skip) skip="$skip $2"; shift ;;
      -h|--help) usage; exit 0 ;;
      *) log "unknown option: $1"; exit 2 ;;
    esac
    shift
  done
  [ -f "$G_BASELINE" ] || die "baseline not found: $G_BASELINE"
  if [ "$G_DRY_RUN" -ne 1 ] && [ "$yes" -ne 1 ]; then
    die "repair is destructive; pass --yes (and --allow-super-restore if needed)"
  fi
  case "$MAX_ROUNDS" in ''|*[!0-9]*) die "--max-rounds must be a positive integer" ;; esac
  [ "$MAX_ROUNDS" -ge 1 ] || die "--max-rounds must be >= 1"

  if [ -z "$G_IMAGE_DIR" ]; then
    G_IMAGE_DIR=$(baseline_image_dir "$G_BASELINE")
  fi
  [ -n "$G_IMAGE_DIR" ] || G_IMAGE_DIR=$outdir
  G_SKIP=$skip

  if [ "$G_DRY_RUN" -ne 1 ]; then
    require_cmd "$ADB_BIN"
    SERIAL=$G_SERIAL
    adb_run get-state </dev/null >/dev/null 2>&1 || die "device $G_SERIAL not available"
  fi

  mkdir -p "$outdir"
  local round=1 rc=1 prev_fail=""
  local alert_dir="$outdir/alerts"
  while [ "$round" -le "$MAX_ROUNDS" ]; do
    log "=== repair round $round/$MAX_ROUNDS ==="

    # Recovery uses the PREVIOUS round's failing list (round 1 never writes).
    if [ "$round" -eq 1 ]; then
      if [ "$G_DRY_RUN" -ne 1 ]; then
        device_sync
        drop_page_cache
        [ "$cold" -eq 1 ] && cold_reboot
      else
        if [ "$cold" -eq 1 ]; then
          log "[dry-run] would sync + drop page cache + cold reboot"
        else
          log "[dry-run] would sync + drop page cache"
        fi
      fi
    else
      G_FAILLIST=$prev_fail
      repair_apply_backups || true
      if [ "$G_DRY_RUN" -eq 1 ]; then
        log "[dry-run] restore step simulated (no writes)"
      fi
    fi

    G_REPORT="$outdir/repair-round$round-$TS.log"
    G_FAILLIST="$G_REPORT.fail"
    set +e
    do_check
    rc=$?
    set -e
    prev_fail=$G_FAILLIST
    printf 'ROUND_REPORT=%s\n' "$G_REPORT"
    if [ "$rc" -eq 0 ]; then
      log "repair SUCCESS: AVB content consistent after round $round"
      printf 'REPAIR_RESULT=CONSISTENT rounds=%s report=%s\n' "$round" "$G_REPORT"
      log_persist "repair serial=$G_SERIAL baseline=$G_BASELINE result=consistent rounds=$round"
      if [ "$round" -gt 1 ]; then
        cmd_alert "GhostLock AVB guard: content restored to baseline after $round round(s)." --outdir "$alert_dir" || true
      fi
      exit 0
    fi

    cmd_alert "GhostLock AVB guard: AVB content mismatch on $G_SERIAL after repair round $round/$MAX_ROUNDS (see $G_REPORT)" --outdir "$alert_dir" || true
    round=$((round + 1))
  done

  log "repair EXHAUSTED after $MAX_ROUNDS rounds: manual intervention required"
  cmd_alert "GhostLock AVB guard: repair exhausted after $MAX_ROUNDS rounds on $G_SERIAL; MANUAL INTERVENTION required. Evidence: $outdir" --outdir "$alert_dir" || true
  cat >"$outdir/REPAIR-MANUAL-$TS.txt" <<EOF
GhostLock AVB guard: manual intervention required
serial=$G_SERIAL
baseline=$G_BASELINE
image_dir=$G_IMAGE_DIR
rounds=$MAX_ROUNDS
time=$(now_iso)
The AVB-verified content still differs from the baseline after the repair loop.
Do NOT keep the device in an unstable state. Options:
  1. inspect the latest round report(s) under $outdir;
  2. use fastboot to flash the affected physical partitions (safest for super);
  3. re-run 'avb_guard.sh baseline' once the device is stable.
EOF
  printf 'REPAIR_RESULT=MANUAL_INTERVENTION rounds=%s report=%s\n' "$MAX_ROUNDS" "$outdir"
  log_persist "repair serial=$G_SERIAL baseline=$G_BASELINE result=manual_intervention rounds=$MAX_ROUNDS"
  exit 1
}

# ---- file-level (page cache) guard -----------------------------------------
# CVE-2026-43284 patches *page cache* pages, not the backing block device.
# A partition hash reads the block device and therefore cannot see such a
# patch; only a read of the target *file* (through the page cache) can.
# files-baseline/files-check hash the target files on the device with
#   su cat <path> | sha256sum
# so a page-cache patch is observable.  See avb-guard-workflow.md section 14.
#
# Default file targets: the two upstream patch sites plus the four upstream
# default vendor carriers.  Device-actual carriers can be added with
# --vendor-scan (enumerate /vendor/lib64) and --extra <path>.
FILE_FIXED_TARGETS="/apex/com.android.runtime/bin/crash_dump64 /system/lib64/libc++.so"
FILE_VENDOR_TARGETS="/vendor/lib64/libbinderdebug.so /vendor/lib64/libstagefrighthw.so /vendor/lib64/libstagefright_aidl_bufferpool2.so /vendor/lib64/libbsp_module.so"
FILES_BASELINE_DIR_DEFAULT="$SCRIPT_DIR/baselines"

# Probe one on-device file through the page cache.  Echoes "<size>\t<sha256>",
# "ABSENT" or "ERR".
device_file_probe() {
  local p=$1 out sz sha
  out=$(adb_run shell "su -c 'if [ -e $p ]; then stat -c%s $p; cat $p | sha256sum; else echo ABSENT; fi'" </dev/null 2>/dev/null | tr -d '\r')
  if [ "$out" = ABSENT ]; then printf 'ABSENT'; return 0; fi
  sz=$(printf '%s\n' "$out" | head -1 | tr -dc '0-9')
  sha=$(printf '%s\n' "$out" | tail -1 | awk '{print $1}')
  if [ -z "$sz" ] || [ -z "$sha" ] || [ "$sha" = "$EMPTY_SHA256" ]; then
    printf 'ERR'; return 0
  fi
  printf '%s\t%s' "$sz" "$sha"
}

# Offline mirror of device_file_probe using --local-root DIR as "/".
local_file_probe() {
  local root=$1 p=$2 full
  full="$root$p"
  if [ ! -e "$full" ]; then printf 'ABSENT'; return 0; fi
  if [ ! -f "$full" ] || [ ! -r "$full" ]; then printf 'ERR'; return 0; fi
  printf '%s\t%s' "$(file_size "$full")" "$(hash_file "$full")"
}

# Ensure adb + root; dies with a clear message otherwise.
require_device_root() {
  local serial=$1 uid
  SERIAL=$serial
  require_cmd "$ADB_BIN"
  adb_run get-state </dev/null >/dev/null 2>&1 || die "device $serial not available via $ADB_BIN"
  uid=$(adb_run shell "su -c 'id -u'" </dev/null 2>/dev/null | tr -d '\r' | head -1)
  [ "$uid" = "0" ] || die "root (su) required on $serial (got uid=$uid)"
}

# Read '# slot=' from a partition baseline, if present.
baseline_slot() {
  if [ -f "$1" ]; then sed -n 's/^# slot=//p' "$1" | head -1; fi
}

# Resolve a single-file avbtool to a Python file path (for import), or "".
resolve_avbtool_file() {
  local p
  if [ -n "${AVBTOOL:-}" ] && [ -f "${AVBTOOL:-}" ]; then printf '%s' "${AVBTOOL:-}"; return; fi
  p="$SCRIPT_DIR/third_party/avbtool/avbtool"
  if [ -f "$p" ]; then printf '%s' "$p"; return; fi
  if command -v avbtool >/dev/null 2>&1; then command -v avbtool; return; fi
  p=$(python3 -c 'import avbtool,sys; print(avbtool.__file__)' 2>/dev/null || true)
  if [ -n "$p" ] && [ -f "$p" ]; then printf '%s' "$p"; return; fi
  printf ''
}

# ---- files-baseline --------------------------------------------------------
cmd_files_baseline() {
  [ $# -ge 2 ] || { usage; exit 2; }
  local serial=$1 outdir=$2; shift 2
  local baseline_out="" extra="" vendor_scan=0 vendor_glob="" vendor_max=0
  local dry=0 local_root="" report=""
  while [ $# -gt 0 ]; do
    case "$1" in
      --baseline-out) baseline_out=$2; shift ;;
      --extra) extra="$extra $2"; shift ;;
      --vendor-scan) vendor_scan=1 ;;
      --vendor-glob) vendor_glob=$2; shift ;;
      --vendor-max) vendor_max=$2; shift ;;
      --local-root) local_root=$2; shift ;;
      --dry-run) dry=1 ;;
      --report) report=$2; shift ;;
      -h|--help) usage; exit 0 ;;
      *) log "unknown option: $1"; exit 2 ;;
    esac
    shift
  done
  case "$vendor_max" in ''|*[!0-9]*) die "--vendor-max must be a non-negative integer" ;; esac

  [ -n "$baseline_out" ] || baseline_out="$FILES_BASELINE_DIR_DEFAULT/$serial-files-baseline.tsv"
  [ -n "$outdir" ] || outdir="$BACKUP_ROOT_DEFAULT/$serial-$(date -u +%Y%m%d)"
  [ -n "$report" ] || report="$outdir/files-baseline.txt"
  mkdir -p "$outdir" "$(dirname "$baseline_out")"
  if [ "$dry" -eq 1 ]; then
    [ -n "$local_root" ] || die "--dry-run needs --local-root DIR (offline mirror of /)"
    [ -d "$local_root" ] || die "local root not found: $local_root"
  else
    require_device_root "$serial"
  fi

  local targets="" seen=" " spec p class
  for p in $FILE_FIXED_TARGETS; do targets="$targets $p|fixed"; done
  for p in $FILE_VENDOR_TARGETS; do targets="$targets $p|vendor"; done
  if [ "$vendor_scan" -eq 1 ]; then
    local found="" cnt=0
    if [ "$dry" -eq 1 ]; then
      found=$(find "$local_root/vendor/lib64" -maxdepth 1 -type f -name '*.so' 2>/dev/null | sed "s|^$local_root||" | sort)
    else
      found=$(adb_run shell "su -c 'ls /vendor/lib64/*.so 2>/dev/null'" </dev/null 2>/dev/null | tr -d '\r' | sort)
    fi
    for p in $found; do
      if [ -n "$vendor_glob" ]; then
        case "$(basename "$p")" in $vendor_glob) ;; *) continue ;; esac
      fi
      targets="$targets $p|vendor-scan"
      cnt=$((cnt + 1))
      if [ "$vendor_max" -gt 0 ] && [ "$cnt" -ge "$vendor_max" ]; then break; fi
    done
  fi
  for p in $extra; do targets="$targets $p|extra"; done

  local tmp="$outdir/files-baseline.tsv.part"
  {
    printf '# ghostlock files baseline v1 (TSV, columns below)\n'
    printf '# serial=%s\n' "$serial"
    printf '# date=%s\n' "$(now_iso)"
    printf '# columns=path\tclass\tstatus\tsize\tsha256\tsource\n'
    if [ "$dry" -eq 1 ]; then
      printf '# mode=dry-run local_root=%s (local mirror, no device contact)\n' "$local_root"
      printf '# device=LOCAL\n'
    else
      printf '# device=%s\n' "$(prop ro.product.device)"
      printf '# model=%s\n' "$(prop ro.product.model)"
      printf '# release=%s\n' "$(prop ro.build.version.release)"
      printf '# fingerprint=%s\n' "$(prop ro.build.fingerprint)"
      printf '# verifiedbootstate=%s\n' "$(prop ro.boot.verifiedbootstate)"
      printf '# slot=%s\n' "$(prop ro.boot.slot_suffix)"
      printf '# note=read through page cache (su cat PATH | sha256sum)\n'
    fi
    printf '# scope=%s\n' "$targets"
  } >"$tmp"
  : >"$report"
  printf '# ghostlock files baseline serial=%s date=%s dry_run=%s\n' "$serial" "$(now_iso)" "$dry" >>"$report"

  local total=0 oks=0 absents=0 errs=0 probe sz sha status src
  for spec in $targets; do
    p=$(printf '%s' "$spec" | cut -d'|' -f1)
    class=$(printf '%s' "$spec" | cut -d'|' -f2)
    case "$seen" in *" $p "*) continue ;; esac
    seen="$seen$p "
    if [ "$dry" -eq 1 ]; then
      probe=$(local_file_probe "$local_root" "$p")
      src="local:$local_root$p"
    else
      probe=$(device_file_probe "$p")
      src="device:$serial:$p"
    fi
    status=ok; sz="-"; sha="-"
    case "$probe" in
      ABSENT) status=absent ;;
      ERR)    status=err ;;
      *)      sz=$(printf '%s' "$probe" | cut -f1); sha=$(printf '%s' "$probe" | cut -f2)
              if [ -z "$sz" ] || [ -z "$sha" ]; then status=err; sz="-"; sha="-"; fi ;;
    esac
    case "$status" in
      ok)     oks=$((oks + 1)); printf '[files-baseline] %-52s class=%-11s size=%-10s sha256=%s\n' "$p" "$class" "$sz" "$sha" ;;
      absent) absents=$((absents + 1)); printf '[files-baseline] %-52s class=%-11s [absent]\n' "$p" "$class" ;;
      *)      errs=$((errs + 1)); printf '[files-baseline] %-52s class=%-11s [ERR]\n' "$p" "$class" ;;
    esac
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$p" "$class" "$status" "$sz" "$sha" "$src" >>"$tmp"
    printf '[%s] %s (%s)\n' "$status" "$p" "$class" >>"$report"
    total=$((total + 1))
  done

  cp "$tmp" "$baseline_out"
  cp "$tmp" "$outdir/files-baseline.tsv"
  rm -f "$tmp"
  printf '# summary total=%s ok=%s absent=%s err=%s\n' "$total" "$oks" "$absents" "$errs" >>"$report"
  log "files baseline written: $baseline_out ($total entries: $oks ok, $absents absent, $errs err); report -> $report"
  printf 'FILES_BASELINE_FILE=%s\n' "$baseline_out"
  printf 'FILES_BASELINE_REPORT=%s\n' "$report"
  [ "$total" -gt 0 ] || die "no file entries captured"
  [ "$oks" -gt 0 ] || die "no readable file entries captured (root? paths?)"
}

# ---- files-check -----------------------------------------------------------
cmd_files_check() {
  [ $# -ge 2 ] || { usage; exit 2; }
  local serial=$1 baseline=$2; shift 2
  local report="" faillist="" dry=0 local_root="" strict=0
  while [ $# -gt 0 ]; do
    case "$1" in
      --report) report=$2; shift ;;
      --fail-list) faillist=$2; shift ;;
      --local-root) local_root=$2; shift ;;
      --strict-errors) strict=1 ;;
      --dry-run) dry=1 ;;
      -h|--help) usage; exit 0 ;;
      *) log "unknown option: $1"; exit 2 ;;
    esac
    shift
  done
  [ -f "$baseline" ] || die "files baseline not found: $baseline"
  if [ "$dry" -eq 1 ]; then
    [ -n "$local_root" ] || die "--dry-run needs --local-root DIR"
    [ -d "$local_root" ] || die "local root not found: $local_root"
  else
    require_device_root "$serial"
  fi
  [ -n "$report" ] || report="$REPORT_DIR_DEFAULT/files-check-$serial-$TS.log"
  [ -n "$faillist" ] || faillist="$report.fail"
  mkdir -p "$(dirname "$report")"
  : >"$report"; : >"$faillist"
  emit() { printf '%s\n' "$*" | tee -a "$report"; }

  emit "# ghostlock files check"
  emit "# serial=$serial baseline=$baseline date=$(now_iso) dry_run=$dry strict_errors=$strict"
  emit "#"

  local total=0 oks=0 fails=0 absents=0 news=0 errs=0
  local p class bstatus bsize bsha bsrc probe cstatus csz csha
  exec 3<"$baseline"
  while IFS=$'\t' read -r p class bstatus bsize bsha bsrc <&3; do
    case "$p" in ''|\#*) continue ;; esac
    total=$((total + 1))
    if [ "$dry" -eq 1 ]; then
      probe=$(local_file_probe "$local_root" "$p")
    else
      probe=$(device_file_probe "$p")
    fi
    cstatus=ok; csz="-"; csha="-"
    case "$probe" in
      ABSENT) cstatus=absent ;;
      ERR)    cstatus=err ;;
      *)      csz=$(printf '%s' "$probe" | cut -f1); csha=$(printf '%s' "$probe" | cut -f2)
              if [ -z "$csz" ] || [ -z "$csha" ]; then cstatus=err; csz="-"; csha="-"; fi ;;
    esac

    if [ "$bstatus" = ok ] && [ "$cstatus" = ok ]; then
      if [ "$csha" = "$bsha" ]; then
        emit "[ok]     $p ($class)"
        oks=$((oks + 1))
      else
        emit "[FAIL]   $p ($class) expected=$bsha got=$csha"
        fails=$((fails + 1)); printf '%s\n' "$p" >>"$faillist"
      fi
    elif [ "$bstatus" = ok ] && [ "$cstatus" = absent ]; then
      emit "[FAIL]   $p ($class) present in baseline but now absent"
      fails=$((fails + 1)); printf '%s\n' "$p" >>"$faillist"
    elif [ "$bstatus" = ok ] && [ "$cstatus" = err ]; then
      emit "[ERR]    $p ($class) baseline hash present but current read failed"
      errs=$((errs + 1))
      if [ "$strict" -eq 1 ]; then printf '%s\n' "$p" >>"$faillist"; fi
    elif [ "$bstatus" = absent ] && [ "$cstatus" = absent ]; then
      emit "[absent] $p ($class) absent in baseline and now"
      absents=$((absents + 1))
    elif [ "$cstatus" = absent ]; then
      emit "[absent] $p ($class) (baseline $bstatus, now absent)"
      absents=$((absents + 1))
    elif [ "$cstatus" = err ]; then
      emit "[ERR]    $p ($class) current read failed (baseline $bstatus)"
      errs=$((errs + 1))
    else
      emit "[new]    $p ($class) unreadable at baseline, now readable"
      news=$((news + 1))
    fi
  done
  exec 3<&-
  emit "#"
  emit "# summary total=$total ok=$oks fail=$fails absent=$absents new=$news err=$errs strict_errors=$strict"
  local rc=0
  if [ "$fails" -gt 0 ]; then rc=1; fi
  if [ "$strict" -eq 1 ] && [ "$errs" -gt 0 ]; then rc=1; fi
  if [ "$rc" -eq 0 ]; then emit "# RESULT=CONSISTENT"; else emit "# RESULT=INCONSISTENT"; fi
  printf 'REPORT_FILE=%s\n' "$report"
  printf 'FAIL_LIST=%s\n' "$faillist"
  log_persist "files-check serial=$serial baseline=$baseline rc=$rc report=$report"
  exit "$rc"
}

# ---- verity-status ---------------------------------------------------------
cmd_verity_status() {
  [ $# -ge 1 ] || { usage; exit 2; }
  local serial=$1; shift
  local report="" no_alert=0 dry=0 outdir="$REPORT_DIR_DEFAULT"
  while [ $# -gt 0 ]; do
    case "$1" in
      --report) report=$2; shift ;;
      --outdir) outdir=$2; shift ;;
      --no-alert) no_alert=1 ;;
      --dry-run) dry=1 ;;
      -h|--help) usage; exit 0 ;;
      *) log "unknown option: $1"; exit 2 ;;
    esac
    shift
  done
  [ -n "$report" ] || report="$outdir/verity-status-$serial-$TS.log"
  mkdir -p "$(dirname "$report")"
  : >"$report"
  emit() { printf '%s\n' "$*" | tee -a "$report"; }

  emit "# ghostlock verity-status serial=$serial date=$(now_iso) dry_run=$dry"
  if [ "$dry" -eq 1 ]; then
    emit "# mode=dry-run (no device contact)"
    emit "# RESULT=SKIPPED"
    printf 'VERITY_RESULT=SKIPPED\n'
    exit 0
  fi
  require_device_root "$serial"
  emit "# device=$(prop ro.product.device) release=$(prop ro.build.version.release)"
  emit "# verifiedbootstate=$(prop ro.boot.verifiedbootstate) vbmeta_device_state=$(prop ro.boot.vbmeta.device_state) slot=$(prop ro.boot.slot_suffix)"
  emit "#"

  local e_count=0 v_count=0 total=0 dm_bin=""
  if adb_run shell "su -c 'command -v dmsetup >/dev/null 2>&1'" </dev/null >/dev/null 2>&1; then
    dm_bin=dmsetup
  elif adb_run shell "su -c 'command -v dmctl >/dev/null 2>&1'" </dev/null >/dev/null 2>&1; then
    dm_bin=dmctl
  fi

  if [ "$dm_bin" = dmsetup ]; then
    local raw vlines tok line
    raw=$(adb_run shell "su -c 'dmsetup status'" </dev/null 2>/dev/null | tr -d '\r')
    vlines=$(printf '%s\n' "$raw" | grep -i 'verity' || true)
    emit "## dmsetup status (verity lines)"
    if [ -n "$vlines" ]; then
      printf '%s\n' "$vlines" | tee -a "$report"
    else
      emit "(none)"
    fi
    while IFS= read -r line; do
      [ -n "$line" ] || continue
      total=$((total + 1))
      tok=$(printf '%s\n' "$line" | sed -n 's/.*verity[,]*[[:space:]]*\([VE]\).*/\1/p' | head -1)
      if [ "$tok" = E ]; then e_count=$((e_count + 1)); else v_count=$((v_count + 1)); fi
      emit "[state] $line"
    done <<EOF2
$vlines
EOF2
  elif [ "$dm_bin" = dmctl ]; then
    local names table n st tok
    names=$(adb_run shell "su -c 'dmctl list devices'" </dev/null 2>/dev/null | tr -d '\r' | awk -F: '/^[A-Za-z0-9._-]+[[:space:]]*:/{gsub(/[[:space:]]+$/, "", $1); print $1}' | sort -u)
    table=$(adb_run shell "su -c 'dmctl list devices -v'" </dev/null 2>/dev/null | tr -d '\r')
    emit "## dmctl list devices -v (table; verity target parameters)"
    if [ -n "$table" ]; then printf '%s\n' "$table" | tee -a "$report"; else emit "(empty)"; fi
    emit "## dmctl status <name> (runtime V/E)"
    for n in $names; do
      st=$(adb_run shell "su -c 'dmctl status $n'" </dev/null 2>/dev/null | tr -d '\r')
      case "$st" in *verity*) ;; *) continue ;; esac
      total=$((total + 1))
      tok=$(printf '%s\n' "$st" | sed -n 's/.*verity,[[:space:]]*\([VE]\).*/\1/p' | head -1)
      if [ "$tok" = E ]; then
        e_count=$((e_count + 1)); emit "[FAIL] $n verity state=E"
      else
        v_count=$((v_count + 1)); emit "[ok]   $n verity state=$tok"
      fi
    done
    emit "# dm_verity_devices=$total state_V=$v_count state_E=$e_count"
  else
    emit "[ERR]  neither dmsetup nor dmctl is available on the device"
    emit "# dm_verity_devices=0 state_V=0 state_E=0"
  fi

  emit "#"
  emit "## dmesg | grep -i verity | tail -n 50"
  local dmesg verr=0
  dmesg=$(adb_run shell "su -c 'dmesg'" </dev/null 2>/dev/null | tr -d '\r' | grep -i 'verity' | tail -n 50 || true)
  if [ -n "$dmesg" ]; then printf '%s\n' "$dmesg" | tee -a "$report"; else emit "(no verity lines)"; fi
  if printf '%s\n' "$dmesg" | grep -Eqi 'error|corrupt|fail|invalid'; then verr=1; fi
  emit "# verity_error_lines=$verr"

  local rc=0
  if [ "$e_count" -gt 0 ] || [ "$verr" -ne 0 ]; then rc=1; fi
  emit "#"
  emit "# verifiedbootstate=$(prop ro.boot.verifiedbootstate)"
  if [ "$rc" -eq 0 ]; then
    emit "# RESULT=OK"
    printf 'VERITY_RESULT=OK\n'
  else
    emit "# RESULT=ERROR"
    printf 'VERITY_RESULT=ERROR\n'
    if [ "$no_alert" -ne 1 ]; then
      ALERT_DIR="$outdir/alerts"
      do_alert "GhostLock AVB guard: dm-verity error on $serial (state_E=$e_count dmesg_error_lines=$verr). Report: $report" || true
    fi
  fi
  log_persist "verity-status serial=$serial rc=$rc report=$report"
  exit "$rc"
}

# ---- avb-verify ------------------------------------------------------------
cmd_avb_verify() {
  [ $# -ge 1 ] || { usage; exit 2; }
  local outdir=$1; shift
  local report="" slot="" run_verify=0
  while [ $# -gt 0 ]; do
    case "$1" in
      --report) report=$2; shift ;;
      --slot|--active-slot) slot=$2; shift ;;
      --verify) run_verify=1 ;;
      -h|--help) usage; exit 0 ;;
      *) log "unknown option: $1"; exit 2 ;;
    esac
    shift
  done
  [ -d "$outdir" ] || die "image dir not found: $outdir"
  [ -n "$report" ] || report="$REPORT_DIR_DEFAULT/avb-verify-$TS.log"
  mkdir -p "$(dirname "$report")"

  require_cmd python3
  local tool; tool=$(resolve_avbtool_file)
  if [ -z "$tool" ]; then
    die "avbtool not found: set AVBTOOL, install avbtool, or use the vendored tools/device-guard/third_party/avbtool/avbtool"
  fi
  local helper="$SCRIPT_DIR/avb_verify_descriptors.py"
  [ -f "$helper" ] || die "helper not found: $helper"

  local vmetas="" n g
  for n in vbmeta_a vbmeta_b vbmeta_system_a vbmeta_system_b; do
    if [ -f "$outdir/$n.img" ]; then vmetas="$vmetas $outdir/$n.img"; fi
  done
  if [ -z "$vmetas" ]; then
    for g in "$outdir"/vbmeta*.img; do
      if [ -f "$g" ]; then vmetas="$vmetas $g"; fi
    done
  fi
  [ -n "$vmetas" ] || die "no vbmeta*.img found in $outdir"

  : >"$report"
  emit() { printf '%s\n' "$*" | tee -a "$report"; }
  local slot_disp="$slot"; [ -n "$slot_disp" ] || slot_disp="auto"
  emit "# ghostlock avb-verify outdir=$outdir slot=$slot_disp date=$(now_iso) verify=$run_verify"
  emit "# avbtool=$tool"

  local vb rslt rc=0
  for vb in $vmetas; do
    emit "### avbtool info_image $(basename "$vb")"
    set +e
    python3 "$tool" info_image --image "$vb" >>"$report" 2>&1
    rslt=$?
    set -e
    emit "### info_image rc=$rslt"
    if [ "$rslt" -ne 0 ]; then rc=1; fi
  done

  emit "#"
  emit "## descriptor digest recompute (hash + hashtree, no key)"
  local vmargs=""
  for vb in $vmetas; do vmargs="$vmargs --vbmeta $vb"; done
  local hrc=0
  set +e
  python3 "$helper" --avbtool "$tool" --image-dir "$outdir" --active-slot "$slot" --tsv "$outdir/avb-verify.tsv" $vmargs >"$outdir/avb-verify.out" 2>&1
  hrc=$?
  set -e
  tee -a "$report" <"$outdir/avb-verify.out"
  emit "# descriptor_recompute rc=$hrc"
  if [ "$hrc" -ne 0 ]; then rc=1; fi

  if [ "$run_verify" -eq 1 ]; then
    emit "#"
    emit "## avbtool verify_image (embedded public key; optional path)"
    for vb in $vmetas; do
      local vrc=0
      set +e
      python3 "$tool" verify_image --image "$vb" >>"$report" 2>&1
      vrc=$?
      set -e
      emit "[verify] $(basename "$vb") rc=$vrc (chain/A-B descriptor lookups may fail offline; the vbmeta signature is still checked)"
    done
  fi

  if [ "$rc" -eq 0 ]; then
    emit "# RESULT=OK"
    printf 'AVB_VERIFY_RESULT=OK\n'
  else
    emit "# RESULT=FAIL"
    printf 'AVB_VERIFY_RESULT=FAIL\n'
  fi
  printf 'REPORT_FILE=%s\n' "$report"
  printf 'AVB_VERIFY_TSV=%s\n' "$outdir/avb-verify.tsv"
  log_persist "avb-verify outdir=$outdir slot=$slot rc=$rc report=$report"
  exit "$rc"
}

# ---- check-all -------------------------------------------------------------
cmd_check_all() {
  [ $# -ge 2 ] || { usage; exit 2; }
  local serial=$1 outdir=$2; shift 2
  local partition_baseline="" files_baseline="" image_dir="" report=""
  local dry=0 local_root="" slot=""
  local skip_partition=0 skip_files=0 skip_verity=0 skip_avb=0
  while [ $# -gt 0 ]; do
    case "$1" in
      --partition-baseline) partition_baseline=$2; shift ;;
      --files-baseline) files_baseline=$2; shift ;;
      --image-dir) image_dir=$2; shift ;;
      --report) report=$2; shift ;;
      --slot) slot=$2; shift ;;
      --local-root) local_root=$2; shift ;;
      --skip-partition) skip_partition=1 ;;
      --skip-files) skip_files=1 ;;
      --skip-verity) skip_verity=1 ;;
      --skip-avb) skip_avb=1 ;;
      --dry-run) dry=1 ;;
      -h|--help) usage; exit 0 ;;
      *) log "unknown option: $1"; exit 2 ;;
    esac
    shift
  done
  [ -n "$partition_baseline" ] || partition_baseline="$BASELINE_DIR_DEFAULT/$serial-avb-baseline.tsv"
  [ -n "$files_baseline" ] || files_baseline="$FILES_BASELINE_DIR_DEFAULT/$serial-files-baseline.tsv"
  [ -n "$image_dir" ] || image_dir="$outdir"
  [ -n "$report" ] || report="$REPORT_DIR_DEFAULT/check-all-$serial-$TS.log"
  mkdir -p "$(dirname "$report")"
  : >"$report"
  emit() { printf '%s\n' "$*" | tee -a "$report"; }

  if [ -z "$slot" ] && [ "$dry" -eq 0 ]; then
    SERIAL=$serial
    if [ -x "$ADB_BIN" ]; then
      slot=$(adb_run shell getprop ro.boot.slot_suffix </dev/null 2>/dev/null | tr -d '\r' | head -1 || true)
    fi
  fi
  [ -n "$slot" ] || slot=$(baseline_slot "$partition_baseline")

  emit "# ghostlock check-all serial=$serial outdir=$outdir date=$(now_iso) dry_run=$dry"
  emit "# partition_baseline=$partition_baseline"
  emit "# files_baseline=$files_baseline image_dir=$image_dir slot=$slot"

  local rc=0
  step() {
    local name=$1; shift
    local r=0
    set +e
    "$@" >>"$report" 2>&1
    r=$?
    set -e
    emit "[step] $name rc=$r"
    if [ "$r" -ne 0 ]; then rc=1; fi
  }

  if [ "$skip_partition" -eq 1 ]; then
    emit "[skip] partition check"
  elif [ ! -f "$partition_baseline" ]; then
    emit "[ERR]  partition baseline missing: $partition_baseline"
    rc=1
  elif [ "$dry" -eq 1 ]; then
    step "partition check" bash "$GUARD_SELF" check "$serial" "$partition_baseline" --dry-run --image-dir "$image_dir" --report "$outdir/check-all-partition.log"
  else
    step "partition check" bash "$GUARD_SELF" check "$serial" "$partition_baseline" --image-dir "$image_dir" --report "$outdir/check-all-partition.log"
  fi

  if [ "$skip_files" -eq 1 ]; then
    emit "[skip] files check"
  elif [ ! -f "$files_baseline" ]; then
    emit "[ERR]  files baseline missing: $files_baseline"
    rc=1
  elif [ "$dry" -eq 1 ]; then
    step "files check" bash "$GUARD_SELF" files-check "$serial" "$files_baseline" --dry-run --local-root "$local_root" --report "$outdir/check-all-files.log"
  else
    step "files check" bash "$GUARD_SELF" files-check "$serial" "$files_baseline" --report "$outdir/check-all-files.log"
  fi

  if [ "$skip_verity" -eq 1 ]; then
    emit "[skip] verity-status"
  elif [ "$dry" -eq 1 ]; then
    emit "[skip] verity-status (dry-run; needs a device)"
  else
    step "verity-status" bash "$GUARD_SELF" verity-status "$serial" --no-alert --report "$outdir/check-all-verity.log"
  fi

  if [ "$skip_avb" -eq 1 ]; then
    emit "[skip] avb-verify"
  elif [ -n "$slot" ]; then
    step "avb-verify" bash "$GUARD_SELF" avb-verify "$image_dir" --slot "$slot" --report "$outdir/check-all-avb.log"
  else
    step "avb-verify" bash "$GUARD_SELF" avb-verify "$image_dir" --report "$outdir/check-all-avb.log"
  fi

  emit "#"
  if [ "$rc" -eq 0 ]; then
    emit "# RESULT=CONSISTENT"
    printf 'CHECK_ALL=CONSISTENT\n'
  else
    emit "# RESULT=INCONSISTENT"
    printf 'CHECK_ALL=INCONSISTENT\n'
  fi
  printf 'REPORT_FILE=%s\n' "$report"
  log_persist "check-all serial=$serial rc=$rc report=$report"
  exit "$rc"
}

# ---- dispatch --------------------------------------------------------------
main() {
  local cmd=${1:-}
  [ $# -gt 0 ] && shift
  case "$cmd" in
    baseline) cmd_baseline "$@" ;;
    check) cmd_check "$@" ;;
    alert) cmd_alert "$@" ;;
    repair) cmd_repair "$@" ;;
    files-baseline) cmd_files_baseline "$@" ;;
    files-check) cmd_files_check "$@" ;;
    verity-status) cmd_verity_status "$@" ;;
    avb-verify) cmd_avb_verify "$@" ;;
    check-all) cmd_check_all "$@" ;;
    ''|-h|--help|help) usage; exit 0 ;;
    *) log "unknown subcommand: $cmd"; usage; exit 2 ;;
  esac
}

main "$@"
