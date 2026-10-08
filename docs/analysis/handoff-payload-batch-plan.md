# Handoff / Payload 同批动作清单（UML · ADR · 需求 · 测试 · 批次）

> 分析类（≤8 KB）。配套 [plan](handoff-payload-plan.md) §10 与 **§9.1 配额策略**、[b0](handoff-payload-b0-contract.md)、[b1b2](handoff-payload-b1b2-runtime.md)、[kotlin](handoff-payload-kotlin.md)、[kotlin-io](handoff-payload-kotlin-io.md)、[params-dispatch](handoff-payload-params-dispatch.md)、[ops](handoff-payload-ops.md)、[plugin-lua](handoff-plugin-lua.md)、[ADR README](adr/README.md)。
> **本文是「同批动作」的唯一权威**（B6 关闭件）：只给**目标文件/章节 + 一句话改什么**，不复述条文。**自包含，不引用任何外部计划**。

## 1. UML 同批（`docs/development/full-process-uml.md`，逐图）

| 图 | 章节 | 一句话改什么 |
|---|---|---|
| IPO | §1 | wire 增「脚本**插入段**以 `bin` 承载（≤64 KB）」；native 增「**攻击前落盘 `GHOSTLOCK_HOME`（0600）**」；Kotlin 只产字节 + sha256 |
| 顶层状态机 | §2.1 | **去掉 terminal 维度**（→ step 执行器）；gate = **`Refused` 10 类具名**（`dirty` 不入 gate） |
| Route 状态机 | §2.5 | 选路输入改「**条目 route 值**」；`RouteResultCode` 不变 |
| Kotlin 配置/运行状态机 | §2.7 | 导入流标为「**展示不拦截**」（L2）；预检置灰 ≠ 权威 |
| C++ Class | §3.1 | 增 `WriteOutcome`、`OpSpec`/`OpRegistry`、`Handle`、`script/` 绑定层；terminal 分组改**执行器** |
| Kotlin Class | §3.2 | 增 `HandoffPlanBuilder`/`GateMirror`/`ScriptStore`/`ScriptTrustView`（`PlanItem` = 7 键） |
| Sequence（端到端） | §4.1 | 标注 gate **插入点**（`state_from` 后 / `run` 前）；落盘画在 gate 前 |
| Sequence（导出） | §4.2 | 导出改为**多 owner 段集合**；`bin`（脚本插入段）路径单列 |
| Sequence（43284 + 插件） | §4.4 | 画**双形态**：`.so`（现有链点）与 **Lua 控制面**（同一 registry/stage/窗口规则） |

- **一处权威**：每张图只画一次；本族文档只链接该文件，不重复画同一结构。

## 2. ADR 同批

| 动作 | 目标 | 一句话改什么 |
|---|---|---|
| **待办（B1）** | `adr/0006` | terminal **轴取消**、词汇保留（→ 执行器）；随实现批次落地 |
| **待办（B1 降级）** | `adr/0004-framework-convergence.md` | **R21**（`DispatchTarget` 由 catalog 编译期投影派生 + 双向 `static_assert`）、**R18**（StepSet 顺序由 alias `static_assert` 绑定）与**多 backend 发射**；**随实现批次落地** |
| **新增** | `adr/0008` | 已建（运行期/沙箱/bin/句柄/gate）——待批准 |
| **新增** | `adr/0009` | 已建（L1/L2/L3、D29 硬线、D30、插件 D31/D32）——待批准 |
| **索引** | `adr/README.md` | 已建；新增/修订 ADR 必须同批更新本表（7 条 ↔ 7 份） |

**B1 降级说明**：ADR-0004/0006 的修订**本轮不动**，改为**随实现批次落地**（避免空头承诺）；落地时同批更新 [ADR README](adr/README.md) 的状态列。

**第五–七轮新增条目（补记）**：

- **第五轮**：plan D20 与扁平 schema 对齐（数据槽移 **op 级** / 脚本 `slot.*`）；`params_ref` 四项定义（第七轮已删）。
- **第六轮**：双上限合并（`kMaxMapMembers` 唯一权威 = 12、删 `kMaxCompositeKeys`）+ 解码层具名；ops 拆出 `-ops-words`。
- **第七轮**：**删 `params_ref` ⇒ 7 键**；**参数两通道**（既有 Owner Schema + 脚本传参）；新增第 10 条静态原因 `executor-unavailable`；`slot.*` 定义；`.sh` 合成/上限裁决（wire 只承载插入段 ≤64 KB）。
- **验收判据**：元素 map >12 ⇒ 具名 `param-invalid`；`QueueElementShape*` 两侧机器对拍；`executor-unavailable` 与 `slot.*` 边界用例。

**超限拆分（独立待办，本轮不动）**：
1. `adr/0004`（16624 B）：拆「正文（决策+判据）+ 附录（迁移批次与证据表）」；附录另存 `adr/0004-appendix.md`，正文 ≤8 KB 留指针。
2. `adr/0001`（20190 B）：A–I findings 明细移交既有 `architecture-findings-register.md`，正文只留分层结论表 + 指针。
3. `adr/0003`（8512 B）：仅超 320 B ⇒ 压缩/移除已 stale 的 GLK1 v2 段落即可。

## 3. requirements 引用（编号已落盘，不重复条文）

- **配置设计族**（本族外，见 `docs/development/requirements.md` 顶部占用注记）：`F21`–`F24`、`I9`–`I11`。
- **payload/handoff 族**：`F25`–`F29`（Lua 运行期 / 导入无限制 / 硬线 D29 / 句柄 D30 / `bin` 仅脚本键）；`I12`–`I14`（vendor 与许可 / 沙箱参数硬约束 / `bin` 键级限制）。
- **编号占用注记**已在 `docs/development/requirements.md` 顶部；新增需求**不得复用**以上号段。

## 4. 测试与对拍同批

| 项 | 内容 |
|---|---|
| manifest 双副本 | 重导（`make -C src profile-manifest-v3`）并保持两份逐字节一致；**不新增 manifest 语义**（R2） |
| golden | **无脚本用例逐字节不变（58 份保）**；**含脚本用例新增 golden**（已核 58 行无脚本条目） |
| 层表登记 | `include_firewall_test.cpp` 增 **`pipeline`**/**`script`**（清单见 b0 §4）；`kWhitelist` 保持为空 |
| 新增 native 测试 | `op_tu_isolation_test`（窗口 TU 禁 include + 无 `_ZTV*`）、`plan_gate_test`（10 条原因）、`QueueElementShapeTest` |
| Kotlin 侧对拍 | `FieldLabelsManifestAgreementTest`、`ManifestWidthCrossModuleTest`、`ControllerInternalsTest`、`StepQueue*`、`MergedQueue*`、`QueueElementShapeAgreementTest` |
| 资源/文案 | `MessageResIdGuardTest` 同步 10 条 `GateReason`（含 `executor-unavailable`）+ 7 项 L2 展示字段 |
| 编解码边界 | `Glkv3EncoderTest`/`Glkv3DecoderTest` 增 `=MAX_BIN_BYTES` 通过 / `+1` 拒绝 |

## 5. 批次顺序与验收判据

| 批次 | 内容 | 验收判据（黑盒/白盒 + 极端值） | 门禁 |
|---|---|---|---|
| **B0.1** | contract 概念与层归属：`write_primitive.hpp` + 层表登记 | 防火墙 0 边 + 空账本 pin；禁层 include ⇒ FAIL | `native-host-tests` |
| **B0.2** | 能力层接线或隔离（`Capabilities` → POD） | `static_assert(!is_polymorphic_v)`；`nm` 无 `_ZTV*`；窗口 TU 禁虚基头 | `native-host-tests` + NDK |
| **B0.3** | 条目 route 编译期选路 | `op_tu_isolation_test`（白盒扫描）；cmp 无间接 call | **cmp + 真机**（动机器码） |
| **B1** | 原语注册 + gate（`OpSpec`/`OpRegistry` 取代 `StepExecution`；10 条） | `plan_gate_test` 逐条极端输入；两处结论冲突 ⇒ 失败（`main.cpp:184`） | `native-host-tests` |
| **B2** | Lua 接入：vendor + 沙箱 + 预算 + 句柄 + 校验器 + `bin` | 超预算 / 越权 API / 伪造句柄 / 非脚本键 `bin` / 插入段 >64 KB ⇒ 均拒 | `native-host-tests` + manifest 行补充 + golden |
| **B3** | 插件 Lua 形态（解冻后）：`_ENV` + 独立预算 + manifest 表 | 单插件超限仅自身失败；未知能力位/非法 stage ⇒ 加载期拒；A 读 B 的 `_ENV` ⇒ 不可达 | 解冻门禁（真机 + 归档） |

- **顺序不可跳**：B0.1 → B0.2 → B0.3 → B1 → B2 → B3；上一批验收未绿不得进下一批（`engineering-rules.md` **R22/R14**（批次纪律））。
- **同批文档动作**（本清单 §1–§4）与对应代码批次**同时**落地：B0.1/B0.2 对应 §1 的 §3.1/§4.1 图与层表；B0.3 对应 §2.5；B1 对应 §2.1/§4.1；B2 对应 §1/bin 与 §4 测试；B3 对应 §4.4 与 `plugin-lua`。

## 6. 标签 → 本族来源

| 标签 | 含义 | 本族权威位置 |
|---|---|---|
| **R1–R9** | native 终验/复验项 | queue-schema §4、b0 §4、b1b2 §B1.2/§B1.3、kotlin-io §2、verification §3 |
| **K-2/K-7/K-9/K-10/K-13** | Kotlin 面项 | kotlin §3/§8、§1、§4、§9；kotlin-io §3 |
| **N*** | native 面项 | b0 §3/§6/§7、b1b2 §B1.2 |
| **B6** | 同批动作清单 | 本文 §1–§5 |
| **D-G** | 执行关键字段 `required` 口径 | params §2、b0 §2 |
| **Q1–Q3** | 配额/引用/单位 | plan §9.1 |
| **C1–C10** | design-critic 项 | queue-schema §1/§2/§7、plan §3/§9.1、b1b2 §B1.2、kotlin §1/§9 |

