# B4 pipeline 落地 真机门禁：run_pipeline + ExploitProcedure 退场（multicast_waiter，direct）— PASS

对应候选 `build/native/ghostlock`（Batch 4 D1=B pipeline 落地，提交 `1241ced`）SHA-256
`bd35b7012151e4b445e6e1e16da4660c6186ede5918f769e0a36a8e1e449a34d`。
（本片 `cmp_disasm` 基线 `fed6b7cf…`，由提交 `722230a` worktree 重建复现，另存
`/private/tmp/ghostlock-b4-pipeline-base`。）

## 设备与入口

- 型号 A301SO；`uname -r` = `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`。
- 入口：direct；KernelSU 未加载冷机；route = multicast_waiter；固定 CPU 对。

## 结果

- PASS（用户执行并确认，2026-09-24）。日志归档于设备 `Download/GhostLock/`；首个归档日志
  第一行为 App 版本/构建日期（提交 `4a319bd`）。

## 变更说明

- backend 步骤（setup/W1/W2/W3）迁入 `session/backend/cve_2026_43499_backend.*`
  （`Cve2026_43499Policy::run` / `attack_write` / `run_setup`）；frontend handoff 经
  `session/root_child_frontend.*`（`RootChildPolicy::run`）；`route/pipeline.hpp` 的
  `run_pipeline<F,B,M>` 组合；`route/orchestrator.hpp` 嵌套 switch 直接分派；
  `ExploitProcedure` 与三个空 procedure 绑定/工厂删除。
- `cmp_disasm`（基线 `fed6b7cf…`）：7 函数 IDENTICAL；`do_one_write` 138 → 132，
  20 个调用目标逐一相同（`this`→参数化 + LTO 参数重排）。
