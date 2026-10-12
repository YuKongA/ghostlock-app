# 43499 · 选项 B 的精确落点（round 84）

## 读到的关键事实（`prepare_page.hpp`）

- `detail::PrepareState` 持有 `pre_fds / post_fds / spray_fds / leak_fd` 与计数 ✓；
- `close_them_all` 里注释原文：*Handled after the walk: **the shaping pair is local to the shaping phase**
  and the leak descriptor is the last close.* ⇒ 即 **shaping 那一对 fd 在相位内开、相位内关** ✗ ⇒ 没有留给后一步的通道 ✓；
- `PrepareOutcome` 只有计数与 `base` ✓，**不含任何 fd 值** ✗；
- 真机印证：`prepare status=ok step=11 opened=6 closed=7 base=0xffff800000008000` ✓。

## 选项 B 的落点（三步同批 ✓）

1. **shaping 相位改为"留一端"**：发送端 fd 不在相位内关闭 ✓，存进 `PrepareState`（新字段 ✓）并在
   `PrepareOutcome` 交出（`shaping_fd` ✓，-1 = 未交出 ⇒ 调用方以**具名拒绝**处理 ✓）；
2. **main 用它发页面**：`payload_base = base + payload_delta`（delta 走 profile/wire ✓）⇒ 页面按退役布局
   构建在**本地缓冲**里（指针全用 `payload_base` ✓）⇒ `send_bytes(fd, buffer, len)` ✓；
3. **同批清理**：mmap 建页 + `alias_delta` ✗ 与 `payload_for_zero_write()` ✗ 一并删除（不再需要 ✓），
   注释与主机测试同步更新 ✓（`payload_page_test` 的 `retired_inputs()` 保留——它仍是对照 ✓）。

## 判据

- 主：`main: victim uid=0` ✓；旁证：comm 落地探针 + `route status=0/0 successes≥1` + 无具名拒绝 ✓；
- 每步跑 `check`/`android`/`lint` ✓；真机门禁归档 ✓。
