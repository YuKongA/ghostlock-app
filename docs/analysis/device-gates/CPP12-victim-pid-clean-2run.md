# CPP12 VictimContext pid 版：干净环境双跑（1 成功 1 崩溃）

- 构建：APK 264；native `b147df9f725873cbcf3fad3d7c68544fe3a6963acd609c72882f76be5950e6ec`（pid 所有权；与 `CPP12k` 构建逐字节相同）
- 环境：KernelSU **未加载**（`/proc/modules` 无 `kernelsu`），Direct 入口，设备重启后冷机
- 日志：
  - 成功：`CPP12-victim-pid-clean-2run-pass.native.log`（286 行，23:59:13，完整至 `KernelSU ready`）
  - 崩溃：`CPP12-victim-pid-clean-2run-crash.native.log`（107 行，00:03:14，截断于 W2 collision 搜索）

## 崩溃位置

| 阶段 | 结果 |
|---|---|
| W1、W1b | 通过（route `clean=1/1`，`private scratch repaired`） |
| W2 attempt 1 | spray 至 `[spray] finding futex collisions... +484ms` 后日志截断、设备重启 |

- 与 `CPP12k` 的两次（W2 spray 窗口）和 `CPP14-release` 首次（W1b spray 后）一致：崩溃集中在 **KernelSnitch collision 搜索 / mm spray 窗口**。

## 运行样本汇总（干净环境）

| 构建 | 运行次数 | 崩溃 | 成功 |
|---|---|---|---|
| `625d5300`（pipe 集合版） | 4（`CPP12m`、`CPP12n`、`CPP14-release` ×2） | 1（W1b spray 后） | 3 |
| `b147df9f`（pid 版） | 2（本次） | 1（W2 collision） | 1 |

- pid 版在干净环境**可以成功**，崩溃位置与 pipe 版相同 → 崩溃是攻击层（`KERNEL-PANIC-01`）的间歇性风险，不是 pid 构建独有。
- 但按“布局敏感改动需连续两次冷机通过”的规则，pid 版（1 成功 1 崩溃）**未通过**；保留已验证的 pipe 集合版本 `625d5300`。

## 结论与后续

- `KERNEL-PANIC-01` 描述收窄：间歇性系统崩溃集中在 W2 KernelSnitch collision/mm spray 窗口（另有 W1b spray 后一例）；同一构建可成功可崩溃；无内核 panic 打印（KernelSU 环境下的崩溃另有 re-enforce 触发证据 `CPP12-victim-pid-kernelsu-crash`）。
- pid 所有权保持回退，待攻击层窗口有缓解手段后重试（登记不变）。
- 门禁环境规则继续生效：必须在 KernelSU 未加载的干净启动下运行。
