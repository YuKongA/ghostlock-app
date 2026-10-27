[English](w3-fail-closed-retry-plan.md) | 简体中文

# W3 失败闭锁重试计划（2026-10-25）

## 当前状态与基线

分支为 `main`，基线为 `e7251a7`，另有未提交的管理器选择批次。在 `cve_2026_43499_backend.cpp` 中，W3 验证失败会返回 `false`，但外层 W2/W3 循环仍可能返回 `StageResult::Continue`；随后 root-child frontend 会在 `chain.seccomp_ok == 0` 时继续运行。当前 profile 还控制 W3 尝试次数和 chain rounds，因此行为不是请求的固定重试策略。

## 目标与约束

- W3 无法验证时闭锁失败。
- 每个 W3 bypass 阶段严格尝试三次。
- 三次 W3 失败后，最多执行两次完整 W2+W3 重启，包括新 victim child 和现有清理/parking 协议；不只重试 seccomp 写入。
- 只有 W3 验证成功，或进程明确没有 seccomp filter 并跳过 W3 时，才进入 root-child handoff。
- 保留 W1、W2 credential 写入、route 选择、profile 字段、Shizuku 行为和资源所有权顺序。
- 不增加 profile 或 wire-format 版本。现有调参字段保留，但 backend 强制安全上限。

## 修改列表

- `src/core/session/backend/cve_2026_43499_backend.cpp`：将 W3 尝试限制为三次，将完整 W2/W3 重启限制为初始运行加两次重启；无 seccomp-free child 验证时返回 `StageResult::Failed`，并在完整重启前明确清理。
- `src/core/session/root_child_frontend.cpp`：增加 handoff 防御前置条件；进程原本有 filter 且 `seccomp_ok` 为 false 时，不释放 root child。
- `src/core/tests/host/backend_dataflow_test.cpp` 及相关 host fixture：覆盖 W3 成功、三次失败后完整重启、两次完整重启失败以及 handoff 闭锁；host link 包含 victim 清理实现。
- `src/Makefile`：将 `victim_context.cpp` 加入 host data-flow harness。
- `src/core/tests/host/route_stub.cpp`：为 backend 实例化提供 host 专用 multicast repair 空实现。
- `docs/analysis/w3-fail-closed-retry-plan_ZH.md`：同步设计记录。

## 数据流与控制流差异

之前：`W2 → W3/profile 次数 → W3 失败 → Continue → root-child frontend`。

之后：`W2 → W3 尝试 1..3 → 成功 → handoff`；三次失败后执行`完整 W2+W3 重启 1..2`；第二次完整重启仍失败时返回 `Failed`，不释放 root child。无 seccomp 的进程保留现有明确跳过 W3 的路径。

## 兼容性与回滚

成功运行继续保持 W1/W2/W3 顺序和 handoff。此前 W3 未验证仍继续的运行现在会在释放 root child 前终止。回滚为回退单个提交；无需 profile 迁移。

## 验证矩阵

- `make -C src native-host-tests`：验证成功、重试、重启和失败闭锁状态机。
- `make -C src ghostlock`：零告警 Android 构建。
- `make -C src lint-tidy`：静态分析。
- `python3 tools/cmp_disasm.py <baseline> build/native/ghostlock`：对比八个攻击函数；检查 W3 资源清理和 handoff 顺序。
- Shizuku 关闭真机门禁：冷机、固定 CPU 对、单 route、KernelSU 未加载；记录一次 W3 成功和一次有界失败，不能重启或 panic。
- Shizuku 开启真机门禁：确认 Shizuku 路径不变且不进入进程内 W3。

## 明确保留

本批次不重新设计 seccomp 目标偏移和写入原语，只修改重试边界、清理和 handoff 门禁。不引入网页来源的 bypass 或新的内核原语。

## 进度

- [x] 探索当前 W3 与 handoff 流程。
- [x] 定义固定重试和重启语义。
- [x] 实现失败闭锁重试状态机。
- [ ] 增加 host 覆盖。
- [ ] 执行构建、lint、反汇编和真机门禁。
