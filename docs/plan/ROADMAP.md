# ROADMAP：排期视图

> **2026-10-21 重写**。旧版（2026-10-19）以 Lua 编排、`R10` 裁定、`B0.0-REVISION`、`core/Makefile`、
> `plan-check` 为骨架，这些对象**已不存在**：Lua/CLI/script 面于 2026-10-17 删除（MASTER-PLAN §0.1）、
> 43284 于 2026-10-19 重置、43499 于 2026-10-20 转 kt-step 生产化。旧版内容留在 git 历史（`git show <rev>:docs/plan/ROADMAP.md`）。
>
> **角色**：本文件只回答「先做什么、后做什么、卡在哪、过什么门」。
> **进度**权威 = `docs/plan/MASTER-PLAN.md`；**批准状态**权威 = `docs/plan/43499-approval-status.md`；
> **执行结构**权威 = `docs/plan/execution-architecture.md`；**事实基线** = `docs/development/introduction.md`；
> **纪律与门槛** = `AGENTS.md`；**决策** = `docs/analysis/adr/`。

## §1 现在在哪（滚动更新 · 2026-10-21）

| 面 | 状态 |
| --- | --- |
| 执行层结构 | **已定稿**：[execution-architecture.md](execution-architecture.md) 合并三份 execution 设计；`PipelineEngine` 是唯一执行权威，`StepEngine`/`StageRunner`/`StagePageCacheEngine` 已删 |
| 43499 kt-step | K0a/K1/K2 /K0b 已实现（`nativeTestHost` 76/76、Kotlin 3/3、APK 只含 `libgl43499syscalls.so`）；**K3 生产 route/页落点未通** |
| 43499 route | multicast：AVD route gate PASS（PI 例外已批）；select：AVD success gate（`successes=1`），计划仍被阻断；tcp：无 route 实现（planning only） |
| 43499 victim | K4a 已备（Kotlin `ProcessVictim` lifecycle，host + AVD）；**无 uid=0 证据** |
| 43284 | 重置设计 v4 + 批次 B0–B6 完成；**B7-A 基线 7/7 Stage OK**；B8/B9 未做；旧计划两件已归档 |
| 文档 | `introduction.md` 已按当前树重写（旧版归档）；ROADMAP/ADR 已更新；`MASTER-PLAN` 仍是唯一进度入口 |
| 门禁债务 | **T4**：`exportJniProtocolManifest` 的生成器不存在 ⇒ `:app:testDebugUnitTest` 需 `-x`；未偿还前不得称全量绿 |
| JNI 重写（待批） | 当前分支改走 Kotlin-first JNI：[43499-43284-jni-rewrite-plan.md](43499-43284-jni-rewrite-plan.md)；批准后本表 §3 的 Now/Next/Later 标 superseded |

## §2 阶段门与硬约束（不可越）

- **G1 宿主**：`./gradlew nativeTestHost` + `./gradlew :app:testDebugUnitTest`；失败数读 `build/test-results/**/*.xml` 全量。
- **G2 设备**：`./gradlew nativeBuildAndroid`（NDK 零告警）+ 真机/AVD 门禁记录归档 `docs/analysis/device-gates/*.md`（单份 ≤8 KB）。
- **G3 文档**：UML/ADR/索引/配额同批更新；一处结构只留一处权威。
- **动攻击路径 ⇒ 真机门禁 + 归档**，缺一不可；**未过 N5/uid=0 不得标 supported**。
- **单写流**：同一时刻只有一个 writer 改被门禁覆盖的文件；共享 `build/**` 与 Gradle 门禁串行。
- **一批一类事**；同批门禁连续失败 2 次即停，回 debug。
- **不新增旁路**：不加 CLI 旗标、不把 fixture 当生产成功、不猜默认值。

## §3 排期（Now / Next / Later）

| 序 | 项 | 门禁 | 依赖 / 阻塞 |
| --- | --- | --- | --- |
| **Now 1** | 43499 **route 页消费者**：把 prepare/shaping fd 送出的页接到 target/cred（multicast 已批 PI 例外） | G2（冷机、固定 CPU 对、单 route、dmesg 无 panic） | 状态表 §3「actor batch 生产启用」= 待批准；计划见 `43499-route-port-plan.md` |
| **Now 2** | 43284 **B8**：payload 脚本白名单化（替换固定占位脚本，root 只读执行） | G1（app 单测 + 防火墙/marker 扫描） | `43284-reset-batch.md` §B8；与 `payload-finish-chain` 白名单冲突须先解 |
| **Next 1** | 43499 **select**：native actor wiring 收口 + 计划解阻 | G2（每 route 单独门禁） | `43499-three-routes-production-plan.md` 仍被 design-critic 阻断 |
| **Next 2** | 43499 **K5**：`victim uid=0` 端到端 + 收尾 | G2 + 设备日志归档 | 依赖 Now 1；状态表 §2 K5 = 待批准 |
| **Next 3** | 43284 **B9**：LKM AVC/Oppo/Defex/BTI、restore_enforce/soft reboot、最终门禁 | G2 + G3 | `43284-reset-batch.md` §B9 |
| **Later 1** | 43499 **tcp_zerocopy**：先出 route 规格再实现 | 规格评审 → G1 → G2 | 状态表 §4 = 待批准；现仅 legacy snapshot 端口 |
| **Later 2** | UI / 配置接线 | G1 | **明确不做**，直到用户批准（kt-step §9） |
| **Later 3** | 文档清理：`full-process-uml.md` §2.1（脚本生命周期）等 Lua 时代章节归档 | G3 | 与结构图权威化同批 |

## §4 待用户裁定（裁定前不得推进）

| # | 事项 | 现状 | 影响 |
| --- | --- | --- | --- |
| 1 | **actor batch 生产启用**（让生产 route 依赖 native 三线程批量 syscall） | 原则允许、生产待批（状态表 §3） | 卡住 Now 1 / Next 2 |
| 2 | **tcp_zerocopy route 方向** | 待批（状态表 §4） | 决定 Later 1 是否立项 |
| 3 | **UI / 配置接线** | 不做（本阶段） | 决定 APK 是否暴露执行入口 |
| 4 | 43284 B8 payload 脚本白名单与 `payload-finish-chain` 的冲突解法 | 未裁 | 卡住 Now 2 |
| 5 | 历史待议（2026-10-17 `PENDING-DECISIONS.md`）中仍有效的条目 | 未逐条复核 | 需要一次收口，避免第二份待议清单 |

## §5 完成定义（DoD）

- 攻击路径项：**真机/AVD 判据通过 + 门禁记录归档**；未过不标 supported。
- 结构项：权威图/权威表各一处；superseded 文档有指针，历史不删。
- 文档项：G3 绿；计划 ≤16 KiB、分析 ≤8 KiB；链接无悬空。
- 临时物：同批清零（AGENTS §3「有界性」）。

## §6 指针

- 进度：[MASTER-PLAN.md](MASTER-PLAN.md) · 批准：[43499-approval-status.md](43499-approval-status.md)
- 结构：[execution-architecture.md](execution-architecture.md) · 事实：[introduction.md](../development/introduction.md)
- 43284：[43284-reset-batch.md](43284-reset-batch.md) + [refactor-design-v4](../analysis/43284-refactor-design-v4.md)
- 纪律：[AGENTS.md](../../AGENTS.md) · 决策索引：[adr/README.md](../analysis/adr/README.md)
- 归档：[docs/archive/README.md](../archive/README.md) + [INDEX-plans](../archive/INDEX-plans.md)
