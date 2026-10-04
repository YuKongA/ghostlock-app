#!/usr/bin/env bash
# GhostLock device partition guard: backup / verify / restore.
# Usage:
#   backup_partitions.sh backup  <serial> <outdir>
#   backup_partitions.sh verify  <serial> <outdir>
#   backup_partitions.sh restore <serial> <outdir> <name>
# Backs up AVB-verified physical partitions + `super` (all logical partitions)
# and the read-only verity views. Requires root (su) on the device.
set -euo pipefail

ADB_BIN=${ADB_BIN:-/Users/nickji/Library/Android/sdk/platform-tools/adb}
# Durable, repo-external backup root (NOT under build/: gradle clean wipes build/).
BACKUP_ROOT=${GHOSTLOCK_BACKUP_DIR:-/Users/nickji/Documents/Program/Analysis/ghostlock-device-backup}
MODE=${1:?mode}
SERIAL=${2:?serial}
OUTDIR=${3:-$BACKUP_ROOT/$2-$(date -u +%Y%m%d)}
NAME=${4:-}
ADB="$ADB_BIN -s $SERIAL"

# Physical, AVB-verified (or boot-critical) partitions, both slots where present.
PHYS="super misc metadata vbmeta_a vbmeta_b vbmeta_system_a vbmeta_system_b \
      boot_a boot_b init_boot_a init_boot_b dtbo_a dtbo_b \
      vendor_boot_a vendor_boot_b recovery_a recovery_b vm-bootsys_a vm-bootsys_b"
# Read-only verity views of the logical partitions (what AVB/dm-verity exposes).
VERITY="system-verity system_ext-verity product-verity vendor-verity odm-verity \
        system_dlkm-verity vendor_dlkm-verity"

dev_for() {
  local n=$1
  if $ADB shell "su -c 'test -e /dev/block/by-name/$n'" >/dev/null 2>&1 </dev/null; then
    echo "/dev/block/by-name/$n"
  else
    echo "/dev/block/mapper/$n"
  fi
}

dump_one() {
  local n=$1 dest="$OUTDIR/$1.img" dev
  dev=$(dev_for "$n")
  $ADB exec-out "su -c 'dd if=$dev bs=4M 2>/dev/null'" > "$dest"
  local sz; sz=$(stat -f%z "$dest" 2>/dev/null || stat -c%s "$dest")
  local sha; sha=$(shasum -a 256 "$dest" | awk '{print $1}')
  printf '%s\t%s\t%s\n' "$n" "$sz" "$sha" >> "$OUTDIR/manifest.tsv"
  echo "[backup] $n size=$sz sha256=$sha"
}

case "$MODE" in
  backup)
    mkdir -p "$OUTDIR"; : > "$OUTDIR/manifest.tsv"
    {
      echo "# ghostlock partition guard"
      echo "# serial=$SERIAL date=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
      echo "# props: state=$($ADB shell getprop ro.boot.verifiedbootstate | tr -d '\r') locked=$($ADB shell getprop ro.boot.vbmeta.device_state | tr -d '\r') slot=$($ADB shell getprop ro.boot.slot_suffix | tr -d '\r')"
    } >> "$OUTDIR/manifest.tsv"
    for n in $PHYS; do $ADB shell "su -c 'test -e $(dev_for $n)'" >/dev/null 2>&1 && dump_one "$n" || echo "[skip] $n (absent)"; done
    for n in $VERITY; do $ADB shell "su -c 'test -e $(dev_for $n)'" >/dev/null 2>&1 && dump_one "$n" || echo "[skip] $n (absent)"; done
    echo "[backup] done -> $OUTDIR/manifest.tsv"
    ;;
  verify)
    set +e
    fail=0
    skip=" ${SKIP_ENTRIES:-} "
    while IFS=$'\t' read -r n sz sha; do
      case "$n" in \#*|'') continue;; esac
      case "$skip" in *" $n "*) echo "[skip] $n (SKIP_ENTRIES)"; continue;; esac
      dev=$(dev_for "$n")
      got=$($ADB exec-out "su -c 'dd if=$dev bs=4M 2>/dev/null'" </dev/null | shasum -a 256 | awk '{print $1}'); rc=$?
      if [[ -z "$got" || $rc -ne 0 ]]; then echo "[ERR]  $n adb_rc=$rc"; fail=1; continue; fi
      if [[ "$got" == "$sha" ]]; then echo "[ok]   $n"; else echo "[FAIL] $n expected=$sha got=$got"; fail=1; fi
    done < "$OUTDIR/manifest.tsv"
    exit $fail
    ;;
  restore)
    : "${NAME:?restore needs <name>}"
    dev=$(dev_for "$NAME")
    $ADB exec-in "su -c 'dd of=$dev bs=4M 2>/dev/null'" < "$OUTDIR/$NAME.img"
    echo "[restore] $NAME written"
    ;;
  *) echo "usage: $0 backup|verify|restore <serial> <outdir> [name]" >&2; exit 2;;
esac
