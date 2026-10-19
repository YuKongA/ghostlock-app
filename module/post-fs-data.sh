# shellcheck shell=sh
# 开机早期装载 ghostlock.ko（与 KernelSU 模块同一时机）。
MODDIR=${MODDIR:-/data/adb/modules/ghostlock}
KO="$MODDIR/lkm/ghostlock.ko"
[ -f "$KO" ] || exit 0
if lsmod | grep -q '^ghostlock '; then exit 0; fi
insmod "$KO" 2>/dev/null || true
