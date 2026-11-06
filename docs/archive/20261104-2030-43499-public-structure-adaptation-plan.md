# 归档说明

- 原路径：`docs/plan/43499-public-structure-adaptation-plan.md`
- 归档日期：2026-11-04 22:24:59 -0500
- 归档原因：old backend/JNI/execution authority; preserve as history；用户于 2026-11-04 批准 Backend root execution D1–D9 与 R0→R1 收口，本文件不再作为活动实施 authority。
- 来源：`dev-nocpp` 当前工作树；正文原样保留。现行唯一后继为 [`../plan/backend-root-execution-unification-plan.md`](../plan/backend-root-execution-unification-plan.md)。

---

# 43499 适配公用结构（payload/finish 由 victim 执行 + 98260 runner/victim 对齐）L 设计 v2

- 状态：**L 设计 v2（2026-10-28，task-69）**；v1（task-66）被 design-critic 判不可（F1–F4/I1–I3），本件按 Lead Q1–Q5 裁定修订；**只写文档、零实现、不 commit**；待 critic 二轮/用户批准后再排实现批。
- 用户指令：「payload 和 finish 要靠 victim 执行，我已经派 98260 全局修改，你 43499 也适配」。
- Lead 裁定（2026-10-28，§7）：payload=FinishScript、无独立用户态 artifact；rooted victim 唯一执行点（Q5 许可 ProcessVictimSession）；log L0 有意 defer；FinishExecution 先 deprecate/无引用/删除另批；保留 su-fallback-refused token。
- 依据 98260 [L v2.1](98260-project-structure-adaptation-plan.md)（§2/§6/§7/§8）；只覆盖 43499 适配，不推 98260 批准位、不改攻击语义、不碰 98260 生产文件。

## 0. 结论摘要

1. **D1（Q1）**：43499 payload=**FinishScript**；内核写 payload 字节（landing/frame/cred，generator 生成、JNI 写入）不是用户态可执行物；**用户态唯一可执行物=FinishScript**，删除“payload 由 victim 执行”歧义。
2. **D2（Q5）**：FinishScript 唯一执行点=**rooted victim**（helper `execScript` 或 Q5 许可的 `ProcessVictimSession.finish`）；禁止 App/UI/`su -c`/`ProcessBuilder` 执行脚本；**超时只 kill 脚本 child、绝不 kill victim**；无 rooted victim 直接 Refused token `cve43499:finish.execute:su-fallback-refused`。
3. **A1/A2 现状**：task-68 A1 初稿已编码（未编译/未跑）、A2 gate 已落；本轮补设计具体化；剩余 A1 delta=`ProcessVictimSession.finish` timeout 当前 `closeLocked()` 会关 victim（`ProcessVictimSession.kt:186-188`）⇒ 改为只 kill 脚本 child、session 只 detach/park。
4. **A0（F4）**：以当前工作树 hash 快照为基线（§4 字段/命令/实测值）；98260 R1a/R1e 未批/未冻结，后续变更⇒A1/A4 重跑。
5. **批序/边界**：A0→A1→A2→A3→A4；Native 零改动、只 43499 专属文件；共享需改⇒停报 Lead；本件零实现、不 commit。

## 1. 现状盘点（file:line，v2 复核）

| 结构块 | 现状 | 目标/差距 |
|---|---|---|
| flow runner | `BackendSpec.runner`（`BackendSpec.kt:70-74`）；dispatcher 只调 `spec.runner`（`BackendRunGate.kt:117-145`）；`Cve43499FlowRunner`（`BackendFlowRunner.kt:44-71`）；注册 `BackendCve202643499.kt:26` | 已对齐（R1a 工作树未批/未冻结）；43499 只做行为回归，不改共享 |
| payload（Q1 后） | 内核 payload 字节（`FinishHandoffAdapter.kt:326,449-462`）；**无用户态 exploit artifact** | 明确不是执行物；未来独立 artifact 另立设计（不覆盖） |
| finish | FinishScript；A1 后 rooted victim `victimFinish`（`Flow43499Impl.kt:1098-1128`；token :1103）；`ProcessVictimSession.finish`（`:171-188`，Q5）；helper 超时 kill 脚本 child（`victim_helper.c:622,696`）；`FinishExecution` deprecated（`ui/.../FinishExecution.kt:27,31`，无生产调用）；RunSpec 字段 deprecated（`Flow43499RunSpec.kt:100-104`）；Adapter 不再构造 executor（`FinishHandoffAdapter.kt:87`） | **A1 delta**：`ProcessVictimSession.finish` 超时当前 `closeLocked()`（`:186-188`）关闭/终止 victim ⇒ 改为 watchdog 只 kill 脚本 child、session detach/park；补回归 |
| victim | core 类型 import（`VictimProbe.kt:3`、`FinishExecutionEvidence.kt:3`）；`VictimIdentity` 仅 `core/victim/VictimIdentity.kt:15`；43499 保留 `EXEC_SCRIPT`，不声明 NO_KILL | 已对齐；A4 只回归 |
| log | `BackendFlowRunner.run` 的 `onLine` 未用于 43499（`BackendFlowRunner.kt:50`） | Q2=L0 **有意 defer**（TD owner=Lead，R1b 冻结后另批 L1）；本轮不碰共享 |

## 2. payload/finish 执行合同（D1–D3，Q1/Q5 写死）

**D1（payload 语义，Q1）**：①内核写 payload（landing/frame/cred 字节，generator 生成、JNI 写入内核页）不是用户态可执行物，不能/不会由 victim 执行；②**用户态唯一可执行物=FinishScript**（`FinishState.toRootScript`→`FinishScriptStore`→`FinishHandoff`），由 rooted victim 执行。删除“payload 由 victim 执行”歧义；未来若需独立用户态 artifact，另立设计（不覆盖）。

**D2（唯一执行点，Q5）**：FinishScript 只允许以下 rooted victim 执行点；**禁止 App、UI、`su -c`、Kotlin `ProcessBuilder` 执行脚本**：
- **D2-A helper**：JNI victim helper `execScript`（`/system/bin/sh <scriptPath>`，W2 成功后 uid=0）；超时 helper watchdog kill 脚本 child（`victim_helper.c:622,696`），victim 保持。
- **D2-B ProcessVictimSession（Q5 许可）**：Kotlin rooted shell victim 的 `finish(scriptPath,timeoutMs)`（`VictimProtocol.finishCommand`）；前提=rooted 证据（`rooted=true`/uid 0）、脚本路径来自 handoff 冻结值、命令经 victim 协议；**超时只允许 victim 内 watchdog kill 脚本 child；session 不得 close/destroy/kill victim，只 park/detach 并返回 timedOut**。
- **D2 可核验定义**：脚本执行 call site 仅允许 `victimFinish(...)`→helper 或 `ProcessVictimSession.finish`；脚本路径必须来自 `FinishHandoff` 冻结 SHA；App/UI 源码不得出现 `ProcessBuilder(...script...)`/`Runtime.exec`/`"su -c"`；无 rooted victim⇒不执行、直接 Refused token。
- **D2 豁免（非脚本执行）**：`FinishAcceptance` 的 `ProcessBuilderFinishCommandSource`（`FinishExecutionEvidence.kt:63`；使用点 `FinishAcceptance.kt:63,157-172`；只跑固定 ksud 证据命令 `KSUD_PATH_CANDIDATES`/`KSUD_VERSION_ARGUMENT`，有界+输出摘要）豁免；`ProcessVictim` 启动 victim 的 ProcessBuilder（`ProcessVictim.kt:72`）豁免；二者不得改为执行 finish 脚本。

**D3（超时/证据/单 W1）**：
- 超时：helper 只 kill 脚本 child（`victim_helper.c:622,696`）并回收；`ProcessVictimSession` 让 victim 内 watchdog kill 脚本 child（`ProcessVictim.kt:206-208` 同语义），session 只 detach/park，**绝不 kill/close victim**（当前 `:186-188` 是 A1 delta）。
- 证据：script sha=App 磁盘字节 vs handoff 冻结值；exit/timedOut/output SHA=**victim 回传**（`VictimFinishResult.Ok`）；marker=App 读回；`FinishAcceptance` 仍要求 binding/resourcesReleased（43499 合同，不放宽/不借用 98260）。
- 单 W1/无 rooted victim：**不执行** App/su，直接 Refused token；异常不 fallback App/su；retained/parked 按既有 closeAll（rooted 只 detach/park，不 kill）。

## 3. 与 98260 公共接口对齐（只读，不改共享）

| 接口 | 43499 采用 | 说明 |
|---|---|---|
| `BackendSpec.runner` | 已注册 `Cve43499FlowRunner`（`BackendCve202643499.kt:26`） | 异常前缀/每 run 新建 Flow/cancel 保留；dispatcher 无 backend 分支；43499 不改共享 |
| `core/victim` 类型 | import `VictimIdentity/VictimResult/VictimError/VictimIdentityReader/VictimCapability` | 无重复定义；R1e 迁移已在树；43499 只回归；残留重复→R1e owner |
| `VictimCapability` | 43499 保留 `EXEC_SCRIPT`，不 append 新位 | `EXEC_SCRIPT`=超时 kill 脚本 child（不 kill victim）；`EXEC_SCRIPT_NO_KILL` 属 98260 |
| FinishScript/FinishAcceptance | 43499 保持自身生成/冻结/验收合同 | 不跨用 98260 类型；TD-R1-2 不扩散 |
| log | L0 有意 defer | 不碰共享 runner；L1（FlowProgress 透传）等 98260 R1b 冻结后另批 |
| R1a/R1e 状态 | 工作树有 R1a/R1e 文件但 **98260 plan §8「用户批准：待签」** ⇒ 未批准/未冻结 | 若后续 R1a/R1e 变更 ⇒ A1/A4 重跑；43499 不编辑共享 |

## 4. 基线（A0，F4）与批次/写范围

**A0 当前工作树快照**（捕获于 2026-10-28T17:01:52Z，v2 编辑前；实现批开始必须重采并比对）：
- 字段与实测值：`head=a9af5482c78a06b8456c3f392806954e655102a1`；`worktree_status_sha256=152254928eb8bb861e360c89f2b832b2f3a8dd87b5d90ddaf4bf20bcf922ede9`；`plan_v1_sha256=77175d87…`；`r1a_paths_sha256=e0969163…`；`r1e_paths_sha256=3ce0b807…`；`98260_plan_sha256=bac0bfc0…`；`writer_inflight=98260（R1a/R1e 未批/未冻结）`。
- 快照命令（read-only）：`git rev-parse HEAD`；`git status --porcelain=v1 -- app/src/main/kotlin app/src/main/cpp app/src/test app/src/debug app/src/androidTest tools docs/plan docs/analysis | LC_ALL=C sort | shasum -a 256`；R1a 两文件与 `core/victim/VictimCapability.kt` 各自 `shasum -a 256`；plan `shasum -a 256`。
- 风险（F4）：98260 R1a/R1e 尚未用户批准/冻结（98260 plan §8「待签」）；若 A0 执行前变化⇒本快照作废、重采并重跑 A1/A4；approval 适配行/G05 风险已标注。

| 批 | 内容 | 文件:节 |
|---|---|---|
| A0 | 快照/写范围检查（read-only）：HEAD+工作树 hash、共享 diff 归属、G01/G06/G07/G08 已实现核对 | 无写入 |
| A1 | finish victim-only（task-68 初稿；delta：`ProcessVictimSession.finish` timeout 不 close victim + “timeout 不 kill victim”回归；Flow/token/RunSpec/Adapter/FinishExecution 语义保持） | `Flow43499Impl.kt:1098-1128`；`ProcessVictimSession.kt:171-188`；`Flow43499RunSpec.kt:100-104`；`FinishHandoffAdapter.kt:87`；`ui/.../FinishExecution.kt:27,31`；测试 |
| A2 | 静态 gate 具体化+F3 负例（§5）；`FinishExecution` 用“无生产引用”判 PASS | NEW/改 `tools/verify_43499_finish_victim_only.py`；扫描 §5 文件集 |
| A3 | log L0（有意 defer；无代码/无共享）；L1 另批等 R1b | 无写入；TD §6 |
| A4 | victim identity/core 迁移回归（不改共享） | 回归测试；不改 core |

**依赖/非冲突**：A1/A2 只 43499 专属；A3/A0 等 R1a/R1e 冻结；A4 只读；任何共享文件需编辑⇒停报 Lead；不 commit。

## 5. 测试/门禁（F3 具体化）

**A2 静态 gate（`tools/verify_43499_finish_victim_only.py`）**
- 扫描集：`backend/cve43499/**` + `ui/screen/module/backends/**` 生产 Kotlin；重点 `Flow43499Impl.kt`（finish 段）、`FinishHandoffAdapter.kt`、`FinishExecution.kt`、`ProcessVictimSession.kt`（Q5 白名单路径）。
- 模式（先剥注释）：`ProcessBuilder(`、`Runtime.exec(`/`getRuntime().exec(`、`"su -c"`/`"su","-c"`、`FinishExecution(` 调用、`finishExecutor?.invoke`/`!!.invoke`；finish timeout 段另扫 `closeLocked(`/`destroy(`/`kill`。
- 豁免（**按调用点/函数，不整文件默认豁免**，带理由）：`FinishExecutionEvidence.ProcessBuilderFinishCommandSource.run`（ksud 证据固定命令）、`ProcessVictim.start`（victim 启动）、`ProcessVictimSession.finish`（Q5 victim 协议执行，仍禁 App/su 模式）、`FinishExecution` 类定义（@Deprecated 定义本身）；`ProcessVictimSession` 其余行受扫描。
- 负例（`--self-test` 必命中，fixture 删除）：① Flow 内 `ProcessBuilder(scriptPath)`；② `Runtime.exec("su","-c")`；③ finish timeout `closeLocked`/`destroy`/`killVictim`；④ 非定义文件出现 `FinishExecution(`；⑤ `finishExecutor?.invoke`；⑥ 无 rooted victim 仍调用执行点；⑦ 合法 ksud 证据/ProcessVictim 启动/ProcessVictimSession.finish 不误报。
- `FinishExecution` PASS=`rg '\bFinishExecution\b'` 排除类定义文件/KDoc 后**无生产引用**（输出空），不是文件内有文本即 PASS；期望输出 `files=<n> exempt=<n> violations=0`、`--self-test: PASS`，失败 EXIT≠0。

**行为/合同/回归**（实现批执行；本轮零实现）：runner parity 既有；A1 拒绝/无触达/rooted victim；timeout 只 kill 脚本 child 且 victim 存活（park/detach）；script sha 不匹配不执行；单 W1 拒绝 token。finish 合同 accepted/refused 矩阵（sha/binding/marker/uid/timedOut/resources），不借用 98260；finish parked/detach 入 G05/E5 回归。identity：无 `backend.cve43499.victim.VictimIdentity` 重复；core Reader 回归。

**离线门禁**（实现批；本轮到文档为止）：compile/UnitTest、过滤单测、NDK（Native 零改动只核对）、scanner r2、`verify_jni_exports`、ABI probe、`git diff --check`。**真机（UNVERIFIED）**：finish 执行者=victim、uid=0、无 App/su、timeout 只杀脚本且 victim 存活、证据对拍；E5/device 前不标 supported。

## 6. CBN/风险/技术债

| 选项 | 说明 | 结论 |
|---|---|---|
| **A. 严格 victim-only+拒绝 App/su（推荐）** | 无 rooted victim 直接 fail；单 W1 不执行 App/su；保留 token；`FinishExecution` 先 deprecate 后删 | 采纳 |
| B. 保持现状（先 App/su 执行再拒绝） | 违反用户“payload/finish 靠 victim 执行”；有 App/su 副作用 | 否决 |
| C. A+立即删 FinishExecution/seam | 更干净但测试/回滚面大 | A1 后另批（TD） |

- **TD**：`FinishExecution`/`finishExecutor` 死路径（Q3：先 @Deprecated/无生产引用，删除另批；owner=Lead）；log L0（Q2：有意 defer，TD owner=Lead，偿还=98260 R1b 冻结后另批 L1）；rooted parked child 无回收（task-45 D-R3f-2 既有 TD）；`ProcessVictimSession.finish` timeout 不 kill victim 的 delta（本设计 A1 必做，实现批记录）。
- **趋势/依赖**：G01 fail-stop（approval:182，已做）在 finish 前触发则不执行 payload/finish；G06/G07（approval:180，已做）影响 preflight；G08（approval:182，已做）CPU pin；**G05（approval:183/186）未冻结/未闭合，不作已闭合依赖**，其实现若改 closeAll 顺序需回归 finish parked/detach；G05/E5 回归清单加 finish parked/detach。98260 R2 若改 core 协议/capability，43499 全回归重跑；本适配不预占 R2/R1b 文件。
- **TD-R1-1**：98260 finish 仍 payload 执行，R2 偿还；43499 不代偿。**TD-R1-2**：两套 FinishAcceptance 合同并存，不放宽/不借用。**TD-R1-4**：98260 victim port seam，43499 不依赖/不改。
- **共享冲突协调**：R1a/R1e 冻结前不编辑共享；A1/A2 只 43499；A3-L1 等 R1b；需共享⇒停报 Lead；每批前后记录 git status/sha。

## 7. 批准记录（Lead 2026-10-28）

| # | 裁定（Lead） | 落位 |
|---|---|---|
| Q1 | 43499 payload=**FinishScript**；无独立用户态 exploit artifact；内核写 payload 字节不由用户态执行；D1 写死；未来独立 artifact 另立设计 | D1/§0.1 |
| Q5 | **许可** `ProcessVictimSession`（Kotlin rooted shell victim）为 victim 执行点；D2=rooted victim（helper `execScript` 或 ProcessVictimSession.finish），禁 App/UI/su；超时只 kill 脚本 child、不 kill victim | D2/D3/§4 A1 |
| Q2 | log L0 **有意 defer**；TD owner=Lead，偿还=98260 R1b 冻结后另批 L1 | §1/§6 |
| Q3 | `FinishExecution` 先 @Deprecated/无引用；删除另批 | §4 A1/§6 |
| Q4 | 保留 token `cve43499:finish.execute:su-fallback-refused` | D2/§4 A1 |

## 8. UNVERIFIED / 不做

- **UNVERIFIED**：用户令「先不要编译，先编码」⇒ 工作树 A1 初稿/现有 gate/测试均**未编译、未跑**；真机 finish 执行者=victim、uid=0、无 App/su、timeout 只杀脚本且 victim 存活、证据对拍、单 W1 拒绝路径、E5 前 supported 均未验证；98260 R1a/R1e 仍未批/未冻结（后续变更须 A1/A4 重跑）。
- **不做**：不写实现（除 A1 已有初稿的后续批）、不编译/不跑门禁（本轮用户令）、不改共享/98260 文件、不改攻击语义/路径门禁、不代偿 TD-R1-1/2/4、不宣称 route supported、不 commit。

## 9. critic F/I 关闭映射

| critic 项 | v2 处置 |
|---|---|
| F1 | D1 写死 Q1；删“payload 由 victim 执行”歧义；§0.1/§2 |
| F2 | D2/D3 按 Q5 写清两种 rooted victim 路径、ProcessVictimSession 约束、ksud 证据/ProcessVictim 启动豁免；§2/§4 A1 |
| F3 | §5 A2 扫描集/模式/按调用点豁免/负例/`FinishExecution` 无引用判据/工具输出 |
| F4 | §4 A0 当前工作树 hash 快照字段+命令+实测值；98260 R1a/R1e 未批/未冻结风险；approval 适配行标注 |
| I1 | G01/G08=approval:182、G06/G07=approval:180、G05=approval:183/186；G05 未冻结不作闭合依赖；parked/detach 入 G05/E5 回归 |
| I2 | Q2 L0 有意 defer+TD（§1/§6/§7） |
| I3 | approval 追加本 plan 行+§6 索引；approval append-only，文档不设字节或篇幅门 |
