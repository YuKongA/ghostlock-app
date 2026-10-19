# shellcheck shell=sh
# 卸载模块时尝试移除内核模块（失败不影响卸载）。
if lsmod | grep -q '^ghostlock '; then
  rmmod ghostlock 2>/dev/null || true
fi
