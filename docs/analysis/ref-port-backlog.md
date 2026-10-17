# 5f184760 → 本仓库 Kotlin 的移植清单

参照实现：`ghostlock-app-ref@5f184760`。它的代码继承自旧线 `6f7ded87`（5f184760 本身是纯文档提交）。
本表是“全面替换”的执行清单：每条给出参照位置、我们的现状、批次与状态。

## 已完成

| 项 | 参照位置 | 我们的落点 | 提交 |
|---|---|---|---|
| 终点无条件恢复钩子 | `steps/chain.cpp:350-362` | `steps::restore_hook` + JNI `restoreHook` + probe 的 finally | `3ba29e38` / `31d91b2b` |
| wait 用可读备用标记 | `real_ops.cpp:408-414` | `.ghostlock_lkm_ok` / `.ghostlock_lkm_fail` | `155d5e4a` |
| wait 预算 15000 | `state.hpp:29-31` | `budgetMs = 15000` | `155d5e4a` |
| 面与门同容量 | `execution_binding.cpp` 门/面表 | `kMaxSurfaces = 8` | `6ac5583a` |
| 模块镜像用已验证那份 | 参照的 bundled ko | `/data/local/tmp/helper.ko` | `0ebbf6d5` |
| 探针与回读抽样（每块 exec 4.00 → 2.06） | `pagecache.cpp:158-165` / `:231` | `verify_every`（默认 32）+ 探针每 512 B | `1d5369d4` |
| patch #1 保持逐块回读 | 参照 `crash_dump.cpp` 同款 | `verify_every = 0`（runner） | `b4b291eb` |

## 批次 1（已完成 · `0f0196f1`）

| 项 | 参照位置 | 现状 | 目标 |
|---|---|---|---|
| seq 三处自动交棒 | `real_ops.cpp:250-257,307-327,339-344` | 每段由 Kotlin 手工 `saInit(seq)` | 成功后按返回序号推进会话 |
| wait 三值统一 15000/40/5 | `state.hpp:29-31` | 40 / 5000 / 15000 三值不一致 | 单点常量 |
| trigger 参数化（双 fork + 500 ms） | `real_ops.cpp:355-379` | 写死 `crash_dump32` + 150 ms | igniter 与 delay 由参数给 |
| guard 策略具名 Skip | `execution_binding.cpp:241` | `guardPolicy=0` 靠猜 | `GUARD_POLICY_SKIP` + 日志 |

## 批次 2：写坏能回退（steps 层已完成 `fdbde604`；runner/JNI 接线进行中）

| 项 | 参照位置 | 现状 |
|---|---|---|
| journal 记录旧块 | `chain.cpp:153-173` | 无（写坏一块即无退路） |
| rollback 倒序写回 | `chain.cpp:285-305` | 无 |
| `rollback_incomplete` 上报 | `stage_runner.cpp:697-742` | 无 |

## 批次 3：计划与载体（进行中 · 闭包校验 + 载体候选表）

| 项 | 参照位置 | 现状 |
|---|---|---|
| plan 闭包 / `target_size` | `chain.cpp:390-435` | 无（Kotlin 1548 行手写流程） |
| 载体候选表 | `execution_binding.cpp:141-148` | 单一路径常量 |
| fillPayloadSlots 的 ko_target 一致性 | `backend_terminal.cpp:321` | 已一致（两处同写 `/vendor/lib64/libbinderdebug.so`） |

## 批次 4：模块终态（进行中 · vermagic 精确公式 + 模块 precheck，新建文件）

| 项 | 参照位置 | 现状 |
|---|---|---|
| 模块 precheck（ELF / `.modinfo` / `__versions` / 签名） | `lkm/lkm_image.cpp` 全套 | 无 |
| `required_vermagic` 精确公式 | `lkm_image.cpp:370-412` | Kotlin 简化版（`2c588a82`），选项串推导待精化 |
| `rewrite_vermagic` 事务语义 | `lkm_image.cpp:574-621` | Kotlin 版已同契约（放不下即不改） |
| 本机实测：同 minor 不同字仍 `match=1` | `contract-design.md:1195` | 已确认（vermagic 不是本机拦路石） |

## 批次 5：窗口（不阻塞 LkmLoaded）

| 项 | 参照位置 | 现状 |
|---|---|---|
| LKM 窗口 open / close(UNLOAD) | `chain.cpp:362-380` | 无（`window opened=0 failed=1`） |

## 状态快照（2026-10-16 23:5x）

| 批次 | 状态 | 证据 |
|---|---|---|
| 1 · seq / wait / trigger / guard | **已完成** | `0f0196f1`，Kotlin 单测 47 例全过 |
| 2 · journal / rollback | steps 层完成，接线进行中 | `fdbde604`；runner/JNI 未传缓冲 |
| 3 · 闭包 / 载体候选 | 进行中 | 新文件未落 |
| 4 · vermagic / precheck | 进行中 | 新文件未落 |
| 5 · LKM 窗口 UNLOAD | 未开始 | 不阻塞 LkmLoaded |

真机侧独立问题（与本表并行）：模块写得进去，但 `insmod` 未把模块带起来；见 runbook §11 的证据表与定性实验。
