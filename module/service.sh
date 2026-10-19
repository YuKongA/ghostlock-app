# shellcheck shell=sh
# late_start 阶段：这里将来启动 ghostlockd（Unix socket 守护进程）。
# 目前后端仍在 APK 的 JNI 里，模块只负责装 LKM，所以此处留空。
MODDIR=${MODDIR:-/data/adb/modules/ghostlock}
[ -x "$MODDIR/bin/ghostlockd" ] && "$MODDIR/bin/ghostlockd" --daemon &
