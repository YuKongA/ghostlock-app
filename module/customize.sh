# shellcheck shell=sh
# GhostLock 模块安装脚本（KernelSU / Magisk 通用）。
#
# 职责：按设备内核挑一个 KMI 镜像拷到 $MODPATH/lkm/ghostlock.ko。
# 说明：这里只做“选镜像”，装载在 post-fs-data.sh，卸载在 uninstall.sh。

ui_print "- GhostLock: 检测 KernelSU 模块环境"
[ -n "$MODPATH" ] || { ui_print "! MODPATH 未设置，退出"; exit 1; }

KMI="$(uname -r | sed -E 's/^([0-9]+\.[0-9]+\.[0-9]+)-android([0-9]+)-(.*)$/android\2-\1/')"
ui_print "- 内核: $(uname -r)"
ui_print "- 推导 KMI: $KMI"

SRC="$MODPATH/lkm/$KMI/ghostlock.ko"
if [ ! -f "$SRC" ]; then
  ui_print "! 找不到 $KMI 的镜像；本模块只带以下 KMI："
  for d in "$MODPATH"/lkm/*; do [ -d "$d" ] && ui_print "    $(basename "$d")"; done
  abort "! 该内核没有对应的 ghostlock.ko"
fi

mkdir -p "$MODPATH/lkm"
cp -f "$SRC" "$MODPATH/lkm/ghostlock.ko"
rm -rf "$MODPATH/lkm"/android*
ui_print "- 已选择 $KMI 镜像"
