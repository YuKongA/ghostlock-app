[English](manager-selection-plan.md) | 简体中文

# 显式管理器选择计划（2026-10-25）

## 当前状态与基线

分支为 `main`，基线为 `e7251a7`。Android 管理器选择器会保存所选包名，但解析仍会把内置 KernelSU 家族包追加到所选包之后。Native root script 也包含硬编码的 `libksud.so` 回退，并检查字面量 `kernelsu` 模块名。因此所选管理器缺失时可能静默使用另一个管理器的 `ksud`；而 KernelSU 家族实现仍共用 `kernelsu` 内核模块身份。

## 目标与约束

- 选择 `Auto` 时保留现有内置优先级。
- 显式选择包时只使用该包的 `libksud.so`，不静默激活其他管理器。
- 保留现有 KernelSU 家族 handoff 协议；除非所选实现不提供相同协议，否则不改 `kernelsu` 模块探测。
- 显式选择找不到 `libksud.so` 时，在进入 Native 攻击路径前失败。
- 保留 W1/W2/W3 顺序、profile 格式、Shizuku 行为和安全模式。

## 修改列表

- `app/src/main/kotlin/com/ghostlock/app/data/ManagerScanner.kt`：有显式选择时只返回该包；`Auto` 仍返回内置列表。
- `app/src/main/kotlin/com/ghostlock/app/data/AndroidGhostlockRepository.kt`：显式管理器解析失败时在启动 Native 前失败；保留 Auto 解析。
- `app/src/main/kotlin/com/ghostlock/app/shizuku/GhostlockUserService.kt`：采用相同的显式解析，并在启动 Native 前报告清晰错误。
- `app/src/test/kotlin/com/ghostlock/app/data/ManagerScannerTest.kt`：断言显式选择不会追加回退管理器。
- `src/core/attack/ops.cpp`：移除生成 root script 中的硬编码管理器回退，只使用选定的 `GHOSTLOCK_KSUD`。
- `docs/analysis/manager-selection-plan_ZH.md`：同步设计记录。

## 数据流与控制流差异

之前：`所选包 → 所选包 + 内置回退 → 第一个 libksud.so → native root script`；环境变量缺失时还可能触发硬编码 KernelSU 包查找。

之后：`Auto → 内置优先级 → 第一个 libksud.so`；`显式包 → 仅该包 → native`；显式路径缺失时在 Native 启动前返回错误。Native handoff 继续使用通用 KernelSU 家族 `ksud late-load` 协议和模块标记。

## 兼容性与回滚

Auto 选择继续兼容当前内置管理器。显式选择变为严格模式，避免意外回退。回滚为回退单个提交；无需修改 profile 或 wire 格式。

## 验证矩阵

- `./gradlew :app:testDebugUnitTest`：管理器解析及 repository/user-service 行为。
- `make -C src native-host-tests`：Native 主机测试。
- `make -C src ghostlock`：零告警 Android Native 构建。
- `make -C src lint-tidy`：静态分析。
- `tools/cmp_disasm.py`：因 root-script handoff 改动而必须执行；对比八个攻击函数并检查生成脚本路径。
- 真机门禁：分别选择 Auto、KernelSU Next 和另一支持管理器；日志必须显示所选 `ksud` 路径且无回退路径。

## 明确保留

不改变攻击阶段、route 选择、profile 二进制格式、seccomp 逻辑、Shizuku 就绪等待和 KernelSU 家族模块身份。

## 进度

- [x] 探索代码与历史。
- [x] 记录基线与设计。
- [x] 实现严格选择。
- [ ] 执行主机/构建/静态验证（环境阻塞已记录在 `build/manager-selection-verification/VERIFICATION.txt`）。
- [ ] 执行真机门禁。
