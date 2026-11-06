# 归档说明

- 原路径：`docs/plan/43499-fast-repair-prebuilt-plan.md`
- 归档日期：2026-11-04 22:24:59 -0500
- 归档原因：old backend/JNI/execution authority; preserve as history；用户于 2026-11-04 批准 Backend root execution D1–D9 与 R0→R1 收口，本文件不再作为活动实施 authority。
- 来源：`dev-nocpp` 当前工作树；正文原样保留。现行唯一后继为 [`../plan/backend-root-execution-unification-plan.md`](../plan/backend-root-execution-unification-plan.md)。

---

# 43499 G05 L 设计 v3：fast-repair 预建页（B1 全收口 + I1/I2/I4/I5）

- 状态：**L 设计 v3（2026-10-28，task-71），只写文档+approval append、零实现、不 commit**；v2 二轮复核判不可（B1 阻断；I1/I2/I4/I5）→本件修订，待三轮复核。
- 约束：不改 Native ABI/不加逻辑；只 Kotlin 生命周期/Flow；不碰 98260；保持 W1/W1b/W2/W3 语义，只前移 fast-repair 的 placement/landing。
- **B1 详细矩阵/时序/负例见附件** [43499-fast-repair-prebuilt-plan-annex.md](43499-fast-repair-prebuilt-plan-annex.md)；本件只留指针与摘要，按职责分离且不设字节或篇幅门。
- 依据：审计 `43499-syscall-reaudit-20261027.md:17`（≈1.4–1.5 s）；review `43499-three-route-line-review-20261027.md:144,153,173`；approval §6.6 的 **G06 行（`P1a parity`）**/**G01 行（`P1b-1`）**（append-only，行号随追加漂移，按标签检索；快照约 :180/:182）。
- 术语：**预建页**=W2 cred 写前完成 repair 链 placement+landing 并保留 owner；**最小事务**=W2 成功后只 `prepareActor`+`executeActor`（+clean close）。

## 0. 结论摘要

1. **目标可行**：repair 目标 `init_cred+8` 为静态 data alias（`Cve43499GeneratorSupport.kt:228-229,272-275`，与 victim 无关）⇒ 预建可行；W2 成功后只需 actor+execute。
2. **B1 已全收口（v3）**：统一 Kotlin helper `requireCleanCloseAfterCred(stage)`，覆盖 **7 类 close**：①W2 链 ②prebuilt activation ③prebuilt discard ④prepareActor/executeActor clean-failure 后 close（`closeChainQuiet` 返回/判定 retained）⑤repair actor ⑥fallback W2_REPAIR 链 ⑦closeAll 聚合；retained/dirty⇒W2 `Failed("cve43499:write.w2:fast-repair-close-retained")`+G01 fail-stop，**禁 W3/finish/acceptance**；`closeAll` 仅在 W3/finish 前预检通过才继续，若晚发现则 acceptance 已不可达（precheck gate 失败→Failed，非“事后 run 失败”）；`failStop.enabled=0` 仍 Failed。详见附件 §A–§D。
3. **B2 保持 v2**：clean failure（discard 成功）⇒W1 继续；full fallback 须 `allowFullFallback=1`+evidence（`w2RepairMode=full-fallback`）+明确 panic 窗口不改善；dirty/retained⇒W1 立即 fail-stop（G01），不进 W1b/W2、不 fallback；closeAll 顺序 **active→prebuilt→victim**。
4. **I1**：引用改现行 **approval §6.6 的 G06 行/G01 行**（`P1a parity`/`P1b-1`，快照约 :180/:182，按标签检索）；G05 前置判据=G06/G01 **实现通过 + E6 真机 evidence**，未跑真机不得当已通过。
5. **I2**：E6 的 `t_afterW2` 补 **activation 子项**（activate 前→prepareActor 前），另报 prepare/execute/close；其余冻结口径保留（`SystemClock.elapsedRealtimeNanos`、含/不含 close、VmHWM App/helper、每 run 原始值、先基线再 3/3、p95=max）。
6. **I4**：B1 矩阵按职责置于附件，主件只留最小指针；主件、附件与 approval 均不设字节或篇幅门，approval 只 append。
7. **I5**：G01 冻结口径=**代码/单测层冻结**（`FailStopController`+Flow 触发+debug conf+单测，approval:180 记录 205/0/0）≠真机门禁闭合；用户令暂不编译 ⇒ 当前只能“可编码准备”，不得称实现门禁已闭；E6 必须含 **G01 close-retained 真机负例**；E5/⑥未过不标 supported；G08 现状=已实现 no-pin sentinel（真机 UNVERIFIED）。
8. **保持**：Q1 默认 B（W1b 后/W2 前）、Q5 `fastRepairPrebuildEnabled=false`、Native 零改动/ABI 不变、E6 PASS/FAIL/UNVERIFIED。

## 1. 现状证据（v2 保留）

| 项 | 位置 | 事实 |
|---|---|---|
| W2/fast-repair | `Flow43499Impl.kt:633,771-791` | W2 `Executed`→`closeLatestChain:774`；`fastRepairEnabled`→`runner.execute(W2_REPAIR,repair)`→close :785；**均未检查 retained** |
| 事务/close | `Cve43499WriteTransactionRunner.kt:62-165`；`Flow:1357-1366` | 5 步（54→63→78→104→129），clean failure 只 `closeChainQuiet()`（吞回执）；`closeLatestChain` retained 时置 evidence+`triggerFailStop` 但调用方继续（failStop.enabled=0 无终止） |
| G01/G06 | `owned/FailStopController.kt:16-78`、`owned/ProcessLimits.kt:47-64`；`Flow:467-477,1357-1366` | G01 只对 dirty/retained 触发（approval §6.6 `P1b-1` 行）；G06 W1 前 soft→hard preflight（approval §6.6 `P1a parity` 行） |
| 路由/证据 | `Cve43499WriteChain.kt:496`；审计 `:17`；review `:144,153,173` | 仅 multicast；当前 W2 后重跑完整 placement ≈1.4–1.5 s；legacy 预建对照见 v2 §1.2（VL main.c:1102-1155/util.c:263-306；L1 backend:66-85/multicast_waiter_route.cpp:121-160；L2 race.cpp:341-377） |

## 2. 目标语义（Definition）

- **D1 目标静态**：repair 目标=`data_alias(init_cred)+8`（`GeneratorSupport.kt:272-275`），不依赖 victim/perf 地址。
- **D2 预建内容**：placement+landing+primitive bytes；**actor 不预建**（3 条 PI 线程/锁不可跨 W2 持有）。
- **D3 W2 最小事务**：W2 成功后只 activate+`prepareActor`+`executeActor`+clean close；不再 preparePlacement/landPage。
- **D4（B1）**：W2 cred 写成功后**任何** close/retained 走统一合同 `requireCleanCloseAfterCred(stage)`；7 类覆盖、reason/step/failStop stage 统一、closeAll 预检与 acceptance gate 见附件 §A–§D。
- **D5（B2）**：clean failure⇒W1 继续；full fallback 仅 `allowFullFallback=1`（默认 false）+evidence+panic 不改善；dirty/retained⇒W1 立即 fail-stop（G01），不进 W1b/W2、不 fallback。
- **D6 closeAll 顺序**：active chains（newest→oldest）→ prebuilt → victim（rooted detach）；聚合 retained 按 D4/附件 §B。
- **D7 复用范围**：仅 multicast；同一 run/boot 单次；不跨 route/victim/boot；不依赖 98260。

## 3. 设计

### 3.1 Kotlin 状态机（`Cve43499PrebuiltRepair`）

`EMPTY→PLACEMENT_READY→PAGE_READY→ACTIVATED→CONSUMED`；任意步 dirty⇒`RETAINED`；clean discard⇒`DISCARDED`；最多 1 个；不在 active chains deque，activation 入列尾；`discardPrebuiltRepair(): PrebuiltDiscardReceipt{clean, retained, termination}`；`closeAll` 聚合含 prebuilt；RETAINED 不 free/不伪报。

### 3.2 Kotlin API/Flow 改动（Native 无新入口）

| 层 | 改动 |
|---|---|
| `Cve43499LifecycleApi` | `prebuildRepairPlacement/landPrebuildRepair/activatePrebuiltRepair/discardPrebuiltRepair: PrebuiltDiscardReceipt/prebuiltRepairAvailable`；`closeAll` 按 D6 |
| `Cve43499WriteTransactionRunner` | `prebuildRepair(stage,request)`；`executePrebuiltRepair(preparation)`；**`closeChainQuiet` 改为返回 `Cve43499ChainCloseReceipt?`**（供 ④ 判定）；`execute(W2_REPAIR)` 保留 fallback |
| 新增 helper（Kotlin-only） | **`requireCleanCloseAfterCred(receipt, stage): String?`**：null=clean；retained⇒返回统一 reason+触发 G01；**所有 7 类 close 必须调用**（附件 §A 表） |
| `Cve43499WriteChainPolicy` | 新增 `fastRepairPrebuildEnabled`（Q5 默认 false）+ `allowFullFallback`（默认 false）；校验 `:447-465` |
| `Flow43499Impl.writeW1Step` | W1 `Executed` 后（默认 W1b 后/W2 前，Q1）：`prebuildRepair`；**clean failure**⇒W1 继续；**retained/discard dirty**⇒W1 立即 fail-stop（不进 W1b/W2） |
| `Flow43499Impl.writeW2Step` | W2 成功：①W2 链 close→helper；②prebuilt 可用⇒`executePrebuiltRepair`（activation/discard/repair/clean-failure close 均 helper）；③不可用且 allowFullFallback⇒`W2_REPAIR`+其 close helper+evidence；④不可用未允许⇒W2 fail（prebuilt-required）；任一 retained⇒W2 fail-stop，禁 W3/finish |
| `Flow43499Impl.lifecycleCloseStep`/acceptance | W2 后立即关闭 W2 相关资源并 **closeAll 预检 gate**；closeAll 仅兜底；若晚发现 retained ⇒ **acceptanceStep 预检 Failed**（reason 同 D4），不进入 Accepted；D6 顺序由 lifecycle 实现 |

### 3.3 Native 接口（不改 ABI）

**Native 零改动、JNI/DTO/ABI 零新增**；Kotlin 组合既有 9 个 JNI 入口：`preparePlacement`(kt:353)/`landPage`(:365)/`prepareActor`(:377)/`executeActor`(:385)/close*(:388-391)→core `prepare_placement`(placement.c:783)/`land_page`(page.c:214)/`prepare_actor`(actor.c:781)/`execute_actor`(actor.c:1008)；ABI/`scan r2` 不变；“Native stash/activate”备选被拒。

### 3.4 失败/取消（摘要；完整矩阵见附件 §C）

| 场景 | 分类 | 行为 |
|---|---|---|
| W1/W1b 后 prebuild placement/land/generate clean failure | clean | W1 继续；W2 成功时：`allowFullFallback=true`⇒完整 fallback+evidence（panic 不改善）；否则 W2 fail（prebuilt-required） |
| prebuild discard/close retained（W1 阶段） | dirty | W1 立即 fail-stop（G01）；不进 W1b/W2；不 fallback |
| W2 cred 写成功后任何 7 类 close retained | dirty | **W2 fail-stop**统一 reason；禁 W3/finish/acceptance；closeAll 仅兜底，晚发现⇒acceptance 预检 Failed |
| W2 RouteMiss（未写成功） | clean | 保留 prebuilt；attempt 耗尽⇒discard⇒W2 fail；dirty⇒对应 fail-stop |
| 取消/退出 | — | closeAll D6；dirty 保留不 free；rooted victim detach（park 非 dirty） |

### 3.5 与 G01/G06/G08/E5 交互（I1/I5）

- **G01（前置）**：G05 实现/门禁前置=**G01 代码/单测层冻结**（`FailStopController`+Flow 触发+debug conf+单测；approval §6.6 `P1b-1` 行记录 205/0/0）；**“可编码准备”≠实现门禁已闭**；真机层（终止/落盘/close-retained 负例）仍 UNVERIFIED，E6 必须包含 G01 close-retained **真机负例**；`failStop.enabled=0` 时 7 类 close retained 仍返回 Failed（B1 不依赖终止）。
- **G06**：已实现（approval §6.6 `P1a parity` 行；`ProcessLimits.kt:47-64`；`Flow:467-477` preflight）；W1 前 preflight 失败⇒G05/E6 不启动；E6 记录 before/after NOFILE/NPROC+峰值 children/fd；Q4 按 G06 真实预算核算。
- **G08**：已实现（no-pin sentinel；首次真机门禁只 pin consumer 为既有计划）；真机 UNVERIFIED；G05 不新增 pin/线程。
- **E5/⑥**：E5 未过不得标 supported；⑥冷机重复（N=3）纳入 G05 evidence；G05 不为 E5 捷径；E5 PASS 要求 `w2RepairMode=prebuilt`（fallback 记 degraded/裁定）。

### 3.6 时机/路由（I3 Q1）

默认 **B（W1b 后/W2 前）**；A（W1 即预建）E6 数据+批准后再切；仅 multicast；select/tcp `fastRepairEnabled=false` 不受影响。

## 4. E6 实验/门禁（I2 冻结口径）

| 项 | 冻结内容 |
|---|---|
| 时钟/起止点 | 统一 `SystemClock.elapsedRealtimeNanos()`；`t_prebuild`=prebuild 调用前→landing 返回（含 placement+generate+land）；**`t_afterW2`=W2 executeActor 返回成功→repair clean close 完成（含 activation+prepare+execute+close）**，子项 `t_activation`（activate 前→prepareActor 前）/`t_repairPrepare`/`t_repairExecute`/`t_repairClose`；`t_panicWindow`=W2 executeActor 返回→repair executeActor 返回（不含 close），另给含 close 对照值 |
| VmHWM/原始值 | 基线=W1 前 App `/proc/self/status:VmHWM`（或 `Debug.MemoryInfo`）+helper（J 路径 pid）分别记录；每 run 记录 App/helper 原始峰值、fd/children；每 run 保存原始 nanos（CSV/evidence） |
| 先基线后阈值 | 第 0 次基线 run（不计 3/3）冻结阈值：`t_afterW2` p50≤300ms/p95≤500ms、`t_panicWindow` p95≤600ms（对照≈1.4–1.5 s，≥60% 缩短）、额外 VmHWM≤+16 MiB、fd 泄漏=0；之后 3 次冷机；**N=3 p95=max** |
| G01/G06 前置 evidence | E6 前：G06 preflight before/after 真机 evidence；E6 内含 **G01 close-retained 真机负例**（test-only debug 注入 retained close，断言 fail-stop/落盘且无 W3；`failStop.enabled=0` 路径同样 Failed）；未跑⇒UNVERIFIED 不标 supported |
| 页稳定性 | 页跨 W1b/W2（A）或仅 W2（B）失效/回收：activation/execute RouteMiss、stale、retained⇒**E6 FAIL**（强制 B/降级） |
| PASS/FAIL | 3/3 满足阈值+无 panic/pstore+`w2RepairMode=prebuilt` 3/3+G01/G06 evidence 齐全⇒PASS；任一失败⇒FAIL；设备离线/无单调时钟/pstore⇒UNVERIFIED |

## 5. CBN/风险（I1/Q4 更新）

| 选项 | 收益 | 成本/风险（G06 已实现后重评） | 翻转条件 |
|---|---|---|---|
| A. W1 即预建（E6 后可选） | placement+landing 全移出 W2 后窗口 | 跨 W1b+W2 持有 1 条额外 placement 链（children/memfd/fd，G06 已抬限仍需核算峰值）；页回收/干扰=E6 判据；W1b 失败浪费 | E6 fd/HWM/页稳定性失败⇒回 B |
| B. W1b 后/W2 前预建（默认） | placement+landing 移出 W2 后窗口；持有仅 W2 | 略少收益；仍需核算 G06 峰值 | E6 失败⇒C/降级 |
| C. legacy-exact（W2 attempt1） | 最接近 L1/VL；风险最小 | 收益中等；不满足“W1 即预建” | 用户接受语义放宽 |
| D. 现状 | 零改动 | panic 窗口≈1.4–1.5 s | 否决 |
| E. Native stash/activate | 少 Kotlin 状态 | 违反 Native 零逻辑/ABI | 否决 |

- **Q4 真实预算**：G06 已 soft→hard（`ProcessLimits.kt:47-64`）；额外 prebuilt 链=一次 placement 的 children+memfd+fd（`PlacementFacts.retainedChildCount/retainedDescriptorCount`，E6 实测）；hard limit 不足/峰值超阈值⇒强制 B/不启用，不以“RLIMIT 缺失”为理由。
- 其它风险：W2 cred 后 repair 失败只能 fail-stop（Q2）；W1 verify UNSUPPORTED 拆墙仍应 prebuild；closeAll 顺序/retained 不伪报；prebuilt 不依赖 victim/perf（victim 仍 W1 后 spawn）。

## 6. 待裁定（v3 更新）

| # | 问题 | 结论/建议 |
|---|---|---|
| Q1 | 预建时机 | **默认 B（W1b 后/W2 前）**；A E6 后再切 |
| Q2 | W2 cred 后 repair 失败 | fail-stop（`allowFullFallback` 只对 W2 前 clean failure 生效） |
| Q3 | E6 阈值 | 先基线 run 冻结再 3/3；不用文档值当门禁 |
| Q4 | 双 chain 预算 | 按 G06 真实 before/after+峰值核算；超限⇒强制 B/不启用 |
| Q5 | `fastRepairPrebuildEnabled` 默认 | **false**（E6/E5 显式打开） |
| Q6（I5） | G05 实现前置 | G01 代码/单测层冻结（≠真机门禁闭合）；E6 含 G01 close-retained 真机负例；E5/⑥ 未过不标 supported；G08 已实现（真机 UNVERIFIED） |

## 7. UNVERIFIED / 不做

- **UNVERIFIED**：1.4–1.5 s 仅审计引用、未复测；页跨 W1b/W2 稳定性；G06 真机 preflight；G01 真机 fail-stop/close-retained 负例；G08 真机 pin；E6/E5/⑥ 未跑；W2 后 repair 失败 panic 边界；prebuilt 命中率；A301SO/mondrian 差异。
- **不做**：不写实现、不改 Native/ABI/DTO、不碰 98260、不改 task-45/46/58/60/66/68 文件、不 commit、不标 supported、不以 G05 替代 G01/G06。
