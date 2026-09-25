# B4 切片3c 真机门禁：middleware hook 收敛为 policy 静态接口（multicast_waiter，direct）— PASS

对应候选 `build/native/ghostlock`（Batch 4 D1=B 切片 3c）SHA-256
`fed6b7cf255b15949e0ed7e1d2cf95f8e81a7f9f71b6ae5ee667693ce918373f`。
（本片 `cmp_disasm` 基线为 `1ac25ff9…`，由提交 `24a583b` worktree 重建复现，另存
`/private/tmp/ghostlock-b4-slice3c-base`。）

## 设备与入口

- 型号 A301SO；`uname -r` = `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`。
- 入口：direct；KernelSU 未加载冷机；route = multicast_waiter；固定 CPU 对。

## 结果

- PASS（用户执行并确认，2026-09-24）。日志归档于设备 `Download/GhostLock/`。

## 变更说明

- `ExploitProcedure` 的 route 虚 hook 收敛为 middleware policy 静态接口：4 个有副作用 hook
  （`resident_write` / `w1_resident_repair` / `w2_fast_repair_prebuild` / `w2_fast_repair_activate`）
  经 `route/middleware_hooks.*` direct dispatch，hook 边界 `[[gnu::noinline]]`；`w3_exact_target` /
  `w1_attempt_cap` / scratch-repair 归属为能力投影。
- `cmp_disasm`（基线 `1ac25ff9…`）：7 函数 IDENTICAL；`do_one_write` 6 行差异（vtable 间接调用 →
  直接调用、optional 检查 ABI 等价），逐条复核；调用顺序与资源准备/回收顺序不变。
