#!/system/bin/sh
# GhostLock 43284-chain root-side command.
#
# Executed by ghostlock.ko through call_usermodehelper (uid 0, kernel context)
# after the module has set SELinux permissive. It only does what the 43284 chain
# needs: find ksud, derive the KMI, run "ksud late-load", wait for the module,
# and leave a marker. Unlike the 43499 root script it performs NO SELinux policy
# fixup (the 43284 path puts SELinux permissive instead of repairing W1 damage).
#
# Markers: /dev/dfm0 = KernelSU loaded, /dev/dfm1 = failure. Log: $LOG below.
HOME_DIR=/data/local/tmp
LOG="$HOME_DIR/.ghostlock_lkm.log"
KSUD="$HOME_DIR/ksud"

echo "[*] lkm cmd start uid=$(id -u) selinux=$(getenforce 2>/dev/null)" >"$LOG"
chmod 644 "$LOG" 2>/dev/null

# The resident channel node is created by ueventd as 0600 root and carries the
# generic u:object_r:device:s0 label (Android ignores miscdevice.mode, and
# /dev/glk is not covered by the vendor file_contexts). The client is an ordinary
# process, so relax BOTH for the session-bound window: ueventd's DAC mode, and
# the SELinux type -- null_device is a chr_file type the shell domain may
# read/write/ioctl. This runs as root while SELinux is still permissive.
chmod 666 /dev/glk 2>/dev/null
chcon u:object_r:null_device:s0 /dev/glk 2>/dev/null
# NOTE(App path): untrusted_app may lack ioctl on null_device; an App-driven run
# needs its own label decision (tracked in docs/analysis/contract-design.md).

# --- ksud discovery (same manager list as the 43499 root script) ---
[ -x "$KSUD" ] || KSUD=$(find /data/app -path '*/me.weishu.kernelsu.pr*/lib/arm64/libksud.so' 2>/dev/null | head -1)
[ -x "$KSUD" ] || KSUD=$(find /data/app -path '*/me.weishu.kernelsu-*/lib/arm64/libksud.so' 2>/dev/null | head -1)
[ -x "$KSUD" ] || KSUD=$(find /data/app -path '*/com.resukisu.resukisu*/lib/arm64/libksud.so' 2>/dev/null | head -1)
[ -x "$KSUD" ] || KSUD=$(find /data/app -path '*/com.kowx712.supermanager*/lib/arm64/libksud.so' 2>/dev/null | head -1)
[ -n "$KSUD" ] || KSUD=/data/local/tmp/ksud
[ -x "$KSUD" ] || KSUD=/data/adb/ksu/bin/ksud
echo "[*] ksud=$KSUD" >>"$LOG"

if [ "$(id -u)" -ne 0 ]; then
  echo '[!] not uid 0; aborting' >>"$LOG"
  touch /dev/dfm1 /data/local/tmp/.ghostlock_lkm_fail
  exit 1
fi

if grep -q '^kernelsu[[:space:]]' /proc/modules 2>/dev/null; then
  echo '[+] KernelSU already loaded' >>"$LOG"
  touch /dev/dfm0 /data/local/tmp/.ghostlock_lkm_ok
  exit 0
fi

if [ ! -x "$KSUD" ]; then
  echo '[!] ksud missing' >>"$LOG"
  touch /dev/dfm1 /data/local/tmp/.ghostlock_lkm_fail
  exit 1
fi

KVER=$(uname -r | cut -d. -f1-2)
AVER=$(uname -r | grep -o 'android[0-9]*' | head -1)
if [ -z "$AVER" ] || [ -z "$KVER" ]; then
  echo '[!] cannot parse KMI from uname -r' >>"$LOG"
  touch /dev/dfm1 /data/local/tmp/.ghostlock_lkm_fail
  exit 1
fi
KMI="$AVER-$KVER"
echo "[*] late-load kmi=$KMI ksud=$KSUD" >>"$LOG"

# /dev/df belongs to the payload: it creates it with O_CREAT|O_EXCL as a one shot mutex and
# releases it itself. The retired script never deletes it and upstream only stats it, so this
# script must not either - deleting it re arms a branch that is still in libc++.
chmod 755 "$KSUD" 2>/dev/null
"$KSUD" late-load --kmi "$KMI" --allow-shell >>"$LOG" 2>&1
echo "[*] late-load exit=$?" >>"$LOG"

# --- wait for the KernelSU module ---
for i in $(seq 1 50); do
  if grep -q kernelsu /proc/modules 2>/dev/null; then
    echo '[+] KernelSU module loaded' >>"$LOG"
    # --- bring the userspace side up ---
    # The module alone is not enough: the manager reads ksud's state, so post-fs-data
    # (which creates /data/adb/ksu and loads the sepolicy) and a detached services
    # daemon (which owns the su channel) have to run as well. Both are logged verbatim,
    # and their exit codes decide nothing on their own - the checks below do.
    echo '[*] ksud post-fs-data' >>"$LOG"
    "$KSUD" post-fs-data >>"$LOG" 2>&1
    echo "[*] post-fs-data exit=$?" >>"$LOG"
    echo '[*] ksud services (detached)' >>"$LOG"
    ( setsid "$KSUD" services >>"$LOG" 2>&1 & ) 2>/dev/null || { "$KSUD" services >>"$LOG" 2>&1 & }
    sleep 1
    # The event set the manager expects on a booted device: post-fs-data, services, boot-completed.
    # All three exist in ksud's cli (checked on the device: `libksud.so --help` lists them).
    echo '[*] ksud boot-completed' >>"$LOG"
    "$KSUD" boot-completed >>"$LOG" 2>&1
    echo "[*] boot-completed exit=$?" >>"$LOG"
    echo "[*] ksud version=$("$KSUD" --version 2>/dev/null)" >>"$LOG"
    if [ -d /data/adb/ksu ]; then
      echo '[*] ksu dir=present' >>"$LOG"
    else
      echo '[!] ksu dir=absent' >>"$LOG"
    fi
    touch /dev/dfm0 /data/local/tmp/.ghostlock_lkm_ok
    exit 0
  fi
  sleep 0.1
done
echo '[!] KernelSU module not loaded' >>"$LOG"
touch /dev/dfm1 /data/local/tmp/.ghostlock_lkm_fail
exit 1
