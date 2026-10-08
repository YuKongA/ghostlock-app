#!/system/bin/sh
HOME_DIR='/tmp/ghostlock-root-script-pin'
LOG='/data/local/tmp/.ghostlock_ksu.log'
SAFE_MODE=1
KSUD="$HOME_DIR/ksud"
echo "[*] root script start uid=$(id -u) euid=$(id -u)" >"$LOG"
chmod 644 "$LOG" 2>/dev/null
echo "[*] seccomp=$(grep Seccomp /proc/self/status 2>/dev/null | tr '\n' ' ')" >>"$LOG"
if [ ! -x "$KSUD" ]; then
  KSUD=$(find /data/app -path '*/me.weishu.kernelsu.pr*/lib/arm64/libksud.so' 2>/dev/null | head -1)
fi
if [ ! -x "$KSUD" ]; then
  KSUD=$(find /data/app -path '*/me.weishu.kernelsu-*/lib/arm64/libksud.so' 2>/dev/null | head -1)
fi
if [ ! -x "$KSUD" ]; then
  KSUD=$(find /data/app -path '*/com.resukisu.resukisu*/lib/arm64/libksud.so' 2>/dev/null | head -1)
fi
if [ ! -x "$KSUD" ]; then
  KSUD=$(find /data/app -path '*/com.kowx712.supermanager*/lib/arm64/libksud.so' 2>/dev/null | head -1)
fi
if [ -z "$KSUD" ]; then KSUD=/data/local/tmp/ksud; fi
if [ ! -x "$KSUD" ]; then KSUD=/data/adb/ksu/bin/ksud; fi
echo "[*] ksud=$KSUD" >>"$LOG"
echo "[*] ksud_file=$(ls -l "$KSUD" 2>/dev/null)" >>"$LOG"
echo "[*] uname=$(uname -r)" >>"$LOG"
if [ "$(id -u)" -ne 0 ]; then
  echo '[!] temp su unavailable; aborting' >>"$LOG"
  exit 1
fi
echo "# $(uname -r)" >"$HOME_DIR/.ghostlock_iomem.new"
if cat /proc/iomem >>"$HOME_DIR/.ghostlock_iomem.new" 2>/dev/null && grep -q 'System RAM' "$HOME_DIR/.ghostlock_iomem.new"; then
  mv "$HOME_DIR/.ghostlock_iomem.new" "$HOME_DIR/.ghostlock_iomem"
  chmod 644 "$HOME_DIR/.ghostlock_iomem" 2>/dev/null
  echo "[*] iomem cache: cached $(wc -c <"$HOME_DIR/.ghostlock_iomem") bytes" >>"$LOG"
else
  rm -f "$HOME_DIR/.ghostlock_iomem.new"
  echo '[!] iomem cache: /proc/iomem read failed' >>"$LOG"
fi
DEBUG_DIR=""
dump_debug() {
  [ -n "$DEBUG_DIR" ] || return 0
  mkdir -p "$DEBUG_DIR/pstore" 2>/dev/null || { echo "[!] debug dump: cannot write $DEBUG_DIR" >>"$LOG"; return 0; }
  {
    echo '== uname -a =='
    uname -a
    echo '== /proc/version =='
    cat /proc/version
    echo '== /proc/cmdline =='
    cat /proc/cmdline
    echo '== /proc/modules =='
    cat /proc/modules
    echo '== selinux =='
    getenforce 2>/dev/null
  } >"$DEBUG_DIR/kernel-info.txt" 2>&1
  if ! dmesg >"$DEBUG_DIR/kernel-dmesg.log" 2>&1; then
    echo '[!] dmesg unavailable' >>"$DEBUG_DIR/kernel-dmesg.log"
  fi
  for f in /sys/fs/pstore/*; do
    [ -f "$f" ] || continue
    cp "$f" "$DEBUG_DIR/pstore/" 2>/dev/null
  done
  cp /proc/iomem "$DEBUG_DIR/iomem.txt" 2>/dev/null
  cp "$LOG" "$DEBUG_DIR/ksu.log" 2>/dev/null
  echo "[*] debug dump written to $DEBUG_DIR" >>"$LOG"
}
# W1's 64-bit child pointer makes adjacent booleans non-zero.
echo 0 > /sys/fs/selinux/checkreqprot 2>/dev/null
if grep -q '^kernelsu[[:space:]]' /proc/modules 2>/dev/null; then
  echo '[+] KernelSU already loaded' >>"$LOG"
fi
KVER=$(uname -r | cut -d. -f1-2)
AVER=$(uname -r | grep -o 'android[0-9]*' | head -1)
if [ -z "$AVER" ] || [ -z "$KVER" ]; then
  echo '[!] cannot parse KMI from uname -r' >>"$LOG"
  exit 1
fi
KMI="${AVER}-${KVER}"
# safe mode: disable all modules before exec ksud
if [ "$SAFE_MODE" = "1" ]; then
  echo "[*] safe mode: disabling all modules under /data/adb/modules" >>"$LOG"
  n=0
  for m in /data/adb/modules/*/; do
    [ -d "$m" ] || continue
    if touch "${m}disable" 2>/dev/null; then
      n=$((n+1))
      echo "  disabled ${m}" >>"$LOG"
    fi
  done
  echo "[*] safe mode: $n module(s) disabled" >>"$LOG"
fi
# step 1: restore policy
POLICY=$(mktemp "$HOME_DIR/.ghostlock_policy.XXXXXX") || {
  echo '[!] cannot create policy dump' >>"$LOG"
  exit 1
}
trap 'rm -f "$POLICY"; dump_debug' EXIT
prepare_policy() {
  cat /sys/fs/selinux/policy >"$POLICY" || return 1
  HEADER=$(od -An -tx1 -N24 "$POLICY" | tr -d ' \n')
  case "$HEADER" in
    8cff7cf9080000005345204c696e7578????????????????) ;;
    *) echo '[!] invalid policy header'; return 1 ;;
  esac
  # Restore missing Android netlink flags: bits 30/31, byte 23.
  CONFIG=$(od -An -tu1 -j23 -N1 "$POLICY") || return 1
  [ -n "$CONFIG" ] || return 1
  CONFIG=$(printf '\\0%03o' "$((CONFIG | 192))" || return 1)
  printf '%b' "$CONFIG" | dd of="$POLICY" bs=1 seek=23 count=1 conv=notrunc
}
FIXUP_RC=1
for i in $(seq 1 10); do
  echo "[*] fixup: attempt $i" >>"$LOG"
  if ! prepare_policy >>"$LOG" 2>&1; then
    sleep 2
    continue
  fi
  BEFORE_POLICYLOAD=$(od -An -tu4 -j12 -N4 /sys/fs/selinux/status 2>/dev/null | tr -d ' ')
  load_policy "$POLICY" >>"$LOG" 2>&1 &
  LPID=$!
  (sleep 8; kill -9 $LPID 2>/dev/null) &
  SPID=$!
  wait $LPID 2>/dev/null
  FIXUP_RC=$?
  kill $SPID 2>/dev/null
  if [ "$FIXUP_RC" -eq 0 ]; then
    AFTER_POLICYLOAD=$(od -An -tu4 -j12 -N4 /sys/fs/selinux/status 2>/dev/null | tr -d ' ')
    echo "[*] policyload before=$BEFORE_POLICYLOAD after=$AFTER_POLICYLOAD" >>"$LOG"
    if [ -n "$AFTER_POLICYLOAD" ] && [ "$AFTER_POLICYLOAD" != "$BEFORE_POLICYLOAD" ]; then
      break
    fi
    echo '[!] load_policy returned success without updating SELinux status' >>"$LOG"
    FIXUP_RC=1
  fi
  sleep 2
done
echo "[*] policy fixup rc=$FIXUP_RC" >>"$LOG"
if [ "$FIXUP_RC" -eq 0 ]; then
# load_policy ok: late-load (module init re-enforces); already-loaded restores below
if grep -q kernelsu /proc/modules 2>/dev/null; then
  KSU_ALREADY=1
  echo "[*] kernelsu already loaded; skipping late-load" >>"$LOG"
else
  KSU_ALREADY=0
  if [ ! -x "$KSUD" ]; then
    echo '[!] ksud missing; cannot late-load' >>"$LOG"
    exit 1
  fi
  echo "[*] late-load kmi=$KMI" >>"$LOG"
  chmod 755 "$KSUD" 2>/dev/null
  "$KSUD" late-load --kmi "$KMI" --allow-shell >>"$LOG" 2>&1
  echo "[*] late-load exit=$?" >>"$LOG"
fi
echo "[*] temp su uid=$(id -u); watching kernelsu.ko" >>"$LOG"
KSU_READY=0
for i in $(seq 1 50); do
  if grep -q kernelsu /proc/modules 2>/dev/null; then KSU_READY=1; break; fi
  sleep 0.1
done
if [ "$KSU_READY" -ne 1 ]; then
  echo '[!] KernelSU module not loaded' >>"$LOG"
  exit 1
fi
echo '[+] KernelSU module loaded' >>"$LOG"
if [ "$KSU_ALREADY" -eq 1 ]; then
  echo "[*] kernelsu already loaded; restoring enforcing" >>"$LOG"
  echo 1 > /sys/fs/selinux/enforce 2>/dev/null
fi
else
  echo '[!] fixup failed; SELinux left permissive' >>"$LOG"
fi
