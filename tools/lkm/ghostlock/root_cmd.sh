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

chmod 755 "$KSUD" 2>/dev/null
"$KSUD" late-load --kmi "$KMI" --allow-shell >>"$LOG" 2>&1
echo "[*] late-load exit=$?" >>"$LOG"

# --- wait for the KernelSU module ---
for i in $(seq 1 50); do
  if grep -q kernelsu /proc/modules 2>/dev/null; then
    echo '[+] KernelSU module loaded' >>"$LOG"
    touch /dev/dfm0 /data/local/tmp/.ghostlock_lkm_ok
    exit 0
  fi
  sleep 0.1
done
echo '[!] KernelSU module not loaded' >>"$LOG"
touch /dev/dfm1 /data/local/tmp/.ghostlock_lkm_fail
exit 1
