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
  touch /dev/dfm1
  exit 1
fi

if grep -q '^kernelsu[[:space:]]' /proc/modules 2>/dev/null; then
  echo '[+] KernelSU already loaded' >>"$LOG"
  touch /dev/dfm0
  exit 0
fi

if [ ! -x "$KSUD" ]; then
  echo '[!] ksud missing' >>"$LOG"
  touch /dev/dfm1
  exit 1
fi

KVER=$(uname -r | cut -d. -f1-2)
AVER=$(uname -r | grep -o 'android[0-9]*' | head -1)
if [ -z "$AVER" ] || [ -z "$KVER" ]; then
  echo '[!] cannot parse KMI from uname -r' >>"$LOG"
  touch /dev/dfm1
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
    touch /dev/dfm0
    exit 0
  fi
  sleep 0.1
done
echo '[!] KernelSU module not loaded' >>"$LOG"
touch /dev/dfm1
exit 1
