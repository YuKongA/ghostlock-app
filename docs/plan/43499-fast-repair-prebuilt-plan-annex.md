# 附件 A：G05 B1 close/retained 全收口（v3，2026-10-28）

- 父件：[43499-fast-repair-prebuilt-plan.md](43499-fast-repair-prebuilt-plan.md)（v3，主件只留最小指针；本附件为 B1 唯一细则权威）。状态：**只写文档、零实现、不 commit**；待三轮 critic。
- 范围：W2 cred 写成功后所有 close/retained 统一合同、7 类 close 矩阵、时序/步骤边界、负例矩阵、reason/step/failStop stage、文档债。B2/Q1/Q5/Native 零改动以主件为准。

## A. 统一合同 `requireCleanCloseAfterCred(stage)`

Kotlin-only helper（`backend/cve43499/flow/`，由 Flow/Runner 调用）：

```kotlin
/** @return null=clean；非 null=统一 retained reason（同时已触发 G01 fail-stop）。 */
internal fun requireCleanCloseAfterCred(
    receipt: Cve43499ChainCloseReceipt?,
    stage: String,
): String?
```

行为（冻结）：
1. `receipt == null`（无 close 发生，例如 activation happy path 不 close）⇒ clean，返回 null。
2. `receipt.retained == true` 或回执含 dirty/非 clean 终止 ⇒ 调用 G01 `FailStopController.trigger(stage, retainedReason)`（reason 格式 `fail-stop:<stage>:<retainedReason>`），并返回统一 reason；
3. **`failStop.enabled=0` 时仍返回 reason**（调用方必须 `StepResult.Failed`；B1 不依赖终止）；
4. W2 cred 写成功后任何调用点返回非 null ⇒ Flow W2 step `Failed("cve43499:write.w2:fast-repair-close-retained")`，**不得**继续 victim.probe/W3/finish；若在 acceptance 预检发现 ⇒ acceptance 必须 `Failed`（不可 Accepted）。

## B. 7 类 close 矩阵（全部必须调用 helper）

| # | close 类别 | 产生点（file:line） | 调用/检查时机 | stage token | 失败行为 |
|---|---|---|---|---|---|
| ① | W2 链 close | `Flow43499Impl.kt:774`（`closeLatestChain`） | W2 execute 成功后**立即**；检查 receipt 后进入 repair | `write.w2` | helper 非 null⇒W2 Failed；禁后续 |
| ② | prebuilt activation close | `activatePrebuiltRepair()` 路径内任何 close（happy path 无 close；activation 失败/回滚 close 必须被捕获） | activation 返回/异常路径；activation 前记录 pending；close 回执统一 helper | `write.w2.prebuilt-activation` | 非 null⇒W2 Failed；不进入 prepare/execute |
| ③ | prebuilt discard close | `discardPrebuiltRepair(): PrebuiltDiscardReceipt` | activation 失败、W2 RouteMiss 清理、closeAll 兜底 | `write.w2.prebuilt-discard` | 非 null⇒W2 Failed（或 W1 阶段按 D5 fail-stop） |
| ④ | prepareActor/executeActor clean-failure 后 close | `Cve43499WriteTransactionRunner.kt:62-165` 的 `closeChainQuiet()`（`ActorPrepareStep.CleanFailure`/`ActorExecuteStep.CleanFailure` 等） | 修复最小事务失败路径；**`closeChainQuiet` 必须改为返回 `Cve43499ChainCloseReceipt?`**并由 Flow 调 helper | `write.w2.clean-failure-close` | 非 null⇒W2 Failed；禁止静默吞回执 |
| ⑤ | repair actor close | `executePrebuiltRepair` 成功后 `closeLatestChain` | repair execute 返回成功后立即 | `write.w2.repair-close` | 非 null⇒W2 Failed |
| ⑥ | fallback W2_REPAIR 链 close | `Flow43499Impl.kt:785`（`closeLatestChain`） | `allowFullFallback=true` 的完整事务成功后立即 | `write.w2.fallback-close` | 非 null⇒W2 Failed；不得当作 fallback 成功 |
| ⑦ | closeAll 聚合 | `Cve43499Lifecycle.closeAll` 回执（`allReleased/retained/termination`） | **(a) W2 后、victim.probe/W3/finish 前预检**（只关 W2 相关链，不关 victim）；**(b) lifecycle close step 仅兜底** | `lifecycle.close` | (a) retained⇒W2 Failed/禁 W3/finish；(b) 晚发现⇒acceptanceStep 预检 Failed（reason 同 D4），不得仅“事后 run 失败” |

## C. 时序/步骤边界（冻结）

```text
W2 cred write success（Flow:774 前）
 → ① close W2 chain; requireCleanCloseAfterCred("write.w2")
 → 若启用 prebuilt repair：
     ② activate;  requireClean(activation close, "write.w2.prebuilt-activation")
        ├ 失败 → ③ discard; requireClean("write.w2.prebuilt-discard"); W2 fail-stop
        └ 成功 → ④ prepareActor（clean-failure close: "write.w2.clean-failure-close"）
                    → executeActor
                    → ⑤ repair close; requireClean("write.w2.repair-close")
 → else if allowFullFallback=true：
     ⑥ W2_REPAIR; requireClean("write.w2.fallback-close")
 → ⑦a closeW2ResourcesAndSeal(): 关闭 W2 相关资源并检查聚合回执 → w2CloseSealed=true
 → victim.probe → W3 → finish
 → lifecycle close step：closeAll（含 victim）仅兜底；若 ⑦b retained：
      acceptanceStep 预检 !w2CloseSealed || retained ⇒ Failed(reason 同 D4)，不得 Accepted
 → acceptance（只有 w2CloseSealed && clean 才可能 Accepted）
```

- **禁 W3/finish**：①②③④⑤⑥及⑦a 任一非 null ⇒ W2 step 立即 `Failed`，`closeAll`/`lifecycle.close` 只做清理；不得进入后续步骤。
- **晚发现（⑦b）**：W3/finish 已发生不可撤回，但 `w2CloseSealed=false` 或 closeAll retained ⇒ acceptanceStep **必须 Failed**（不可达 Accepted），evidence 标记发现阶段；不得只“run 失败但 acceptance Accepted”。
- **closeAll 顺序（D6）**：active chains（newest→oldest）→ prebuilt → victim（rooted detach）；⑦a 只关闭 W2 相关链（含 prebuilt/W2/repair），不触碰 victim 与后续 W3 资源。

## D. reason/step/failStop stage（统一写死）

| 项 | 值 |
|---|---|
| W2 StepResult reason | `cve43499:write.w2:fast-repair-close-retained` |
| acceptanceStep 预检 reason | 同上（`cve43499:write.w2:fast-repair-close-retained`） |
| failStop reason 格式 | `fail-stop:<stage>:<retainedReason>`（`FailStopController.trigger(stage, retainedReason)`） |
| failStop stage | ①②`write.w2`；③`write.w2.prebuilt-activation`/`write.w2.prebuilt-discard`；④`write.w2.clean-failure-close`；⑤`write.w2.repair-close`；⑥`write.w2.fallback-close`；⑦`lifecycle.close` |
| failStop.enabled=0 | helper 仍返回 reason；Flow 仍 `Failed`；不得 fallback/继续 |
| evidence keys | `w2CloseStage`、`w2CloseRetained`、`w2CloseSealed`、`failStopReason/Stage/Triggered/PrecededByRetained` |

## E. 负例矩阵（B1 完整）

| 测试 | 注入 | 断言 |
|---|---|---|
| T1 W2 链 close-retained | fake lifecycle `closeChain()` 返回 retained=true（W2 后首次调用） | Flow W2 `Failed` 统一 reason；failStop fake stage=`write.w2`；无 victim.probe/W3/finish；`enabled=0` 仍 Failed |
| T2 activation close-retained | fake activation 路径 close 回执 retained | W2 Failed；stage=`write.w2.prebuilt-activation`；不进入 prepare/execute |
| T3 prebuilt discard retained | fake `discardPrebuiltRepair` 返回 retained | W2 Failed（或 W1 阶段 fail-stop）；stage=`write.w2.prebuilt-discard`；不 fallback |
| T4 clean-failure close retained | `ActorPrepareStep.CleanFailure` 路径 fake close retained | W2 Failed；stage=`write.w2.clean-failure-close`；不静默吞回执 |
| T5 repair actor close retained | executePrebuiltRepair 成功后 close retained | W2 Failed；stage=`write.w2.repair-close` |
| T6 fallback 链 close retained | allowFullFallback=true，fake W2_REPAIR close retained | W2 Failed；stage=`write.w2.fallback-close`；不视为 fallback 成功 |
| T7a closeAll 预检 retained（W2 后） | fake closeAll 聚合 retained（⑦a） | W2 Failed；无 W3/finish |
| T7b closeAll 晚发现 retained（W3/finish 后） | fake ⑦b retained | acceptanceStep `Failed` 同 reason；**无 Accepted**；evidence 标阶段 |
| T8 W1 dirty（discard retained） | W1 prebuild discard retained | W1 Failed；无 W1b/W2；无 fallback；failStop stage=`write.w1.prebuilt-retained`；`enabled=0` 仍 Failed |
| T9 helper 单测 | null/clean receipt、retained receipt、enabled=0 | null→null；retained→reason+failStop(fake)；enabled=0 仍 reason；无 close 被跳过（静态断言 7 类调用点） |

## F. 文档债/配额（I4）

- 主件 ≤16 KiB、本附件独立 ≤16 KiB；approval 只 append（G05 v3 状态+索引行），文档债 owner=Lead、偿还=三轮通过后按需合入/拆分；主件只保留指针与摘要，B1 细节以本附件为准。

## G. UNVERIFIED

- 7 类 close 均为 source-only；`requireCleanCloseAfterCred` 未实现/未跑；G01 真机 fail-stop/落盘/close-retained 负例 UNVERIFIED；G06 真机 preflight UNVERIFIED；E6/E5/⑥未跑；W2 后 repair 失败 panic 边界未知；closeAll 晚发现时 W3/finish 已执行的证据记录需真机验证。
