# Handoff / Payload 操作细则（ops）

> 分析类（≤8 KB）。配套 [plan](handoff-payload-plan.md)、[params](handoff-payload-params.md)、[**ops-words**](handoff-payload-ops-words.md)（op 词表）。**自包含，不引用任何外部计划**。

## 1. 操作词表

> **已拆分**：13 个 op 的入口/写模式/route 依赖/重试来源/前后置可观测条件见 [handoff-payload-ops-words.md](handoff-payload-ops-words.md)（唯一权威）。本文只保留**判据、诊断表与 gate 插入点**。

## 2. 要点（钉死 op 边界）

1. **`write.seccomp` 拆两枚且背靠背**：`fork()` 在 `mode != 0` 时重新武装 `TIF_SECCOMP` ⇒ `flags`（:242-244）与 `mode`（:253-255）都要清零，中间只隔一次 settle（:251）。
2. **`probe.leaf` 仅非 tcp**：tcp 的 `M::w3_exact_target == true`（:207）直接命中；只有 pselect 回退需 comm 探针区分 `[target]`/`[target+8]`（:198-200、:212-216）。
3. **`probe.leaf` 参数硬编码**：`attempts=4`、`settle_us=50000` 在调用点（:215），不来自 profile ⇒ D24 迁入 Owner Schema。
4. **`drain` 不独立成 op**：4 个 `slab_drain()` 语义不同——每次写首次尝试前（:86）、W3 首轮（:240）、W1W3 进 W2 前（:382）、W1W2 进 W2 前（:417）⇒ 归**重试块属性**。
5. **`handoff.root_child` 保持一枚**：settle→启动→探针→结论在同一函数（root_child.cpp:41-104）；拆开会让「临时 root 但模块未加载」失去单一结论点。

## 3. 参数来源（op → Schema 键 → 默认）

| op | Schema 键（owner） | manifest `default` | accessor 默认（代码） |
|---|---|---|---|
| `write.*` / `repair.scratch` | `backend.cve_2026_43499.execution.stages.*`（w1/w2/w3 attempts/settle + `w1_scratch_repair_attempts`/`w3_chain_rounds`） | **`-`** | 无（0 ⇒ D-G required 兜住） |
| `probe.leaf` / `drain` | **无**（硬编码 :215 / 重试块属性） | — | 4/50000 ⇒ **D24 迁入** |
| route 超时 | `...execution.race.route_done_timeout_ms` | `-` | **300000**（`model.hpp:302-306`） |
| handoff | `...execution.handoff.*`（settle/poll 五项） | `-` | 见 `model.hpp` 同名 accessor |

> 口径：manifest `default` 多为 `-`（值由 profile/App 提供）；`literal:N` 仅少数行（如 `backend.cve_2026_43284.execution.*`）。**accessor 默认 ≠ manifest 默认**；D-G（标签映射见 [batch-plan](handoff-payload-batch-plan.md) §6）要求执行关键字段改 manifest `required` 并由 App 填值。
> **队列元素形态**（**7 键**）见 [queue-schema](handoff-payload-queue-schema.md) §1；**参数两通道**见 [params](handoff-payload-params.md)。

## 4. Gate 原因与可复用诊断

**原因码唯一权威 = b1b2 §B1.2**：**10 条静态原因**（含 `executor-unavailable`）（`unknown-op`/`op-not-available`/`route-not-available`/`geometry-missing`/`capability-missing`/`order-violation`/`hash-mismatch`/`param-invalid`/`seam-in-pi-window`）；**`dirty-failure` 移出 gate** ⇒ 运行期终止语义（不换路/不回退）。

本表只给**可复用诊断（file:line）**：

| 用途 | 诊断（file:line） |
|---|---|
| op/step 词表 | `unknown-step-token`（step_plan.hpp:93） |
| route 可用性 | `Unsupported`（route_status.h:9-15）；`route-not-applicable`（step_plan.hpp:99）；`experimental-not-declared`（:105） |
| 几何缺失（D25） | 静默回落 ⇒ 改 `MissingRequired`（schema.hpp:286） |
| 能力缺失 | `CapabilitySet`（**`src/core/contract/capability.hpp:107`**，既有定名；`capabilities.hpp` 是伞头）；`caps_rejected`（plugin/host.hpp:208）；来源 `contract::Capabilities`（core_session.hpp:34） |
| 顺序/依赖 | `missing-dependency`（step_plan.hpp:96）、`not-a-canonical-prefix`（:97） |
| 哈希身份 | `support/sha256.hpp`；`loader.hpp:82`（`sha256_file`） |
| 参数 | `WidthMismatch`（schema.hpp:291）、`UnresolvedToken`（:299）、`params-reserved-…`（step_plan.hpp:88-89） |
| seam 位置 | 纯静态判定（操作之间）；写窗口事实见 `primitives.cpp:145-176` |
| 运行期终止（非 gate） | `is_dirty`（route_status.h:28-30）；`fail_stop_dirty_race`⇒`exit_group(70)`（util.cpp:36-43） |

## 5. 控制器签名草案与 gate 插入点

- `DeviceFacts`：只读设备事实（release/KMI/能力位/SELinux/seccomp），由**组合根**填充（b1b2 §B1.2）。
- `GateVerdict` = `{ok, reason, path, index}`（b1b2 §B1.2 同形）；**只二值，不生成备选**。
- `plan_gate(plan, params, facts)`：逐条目按 §4 判定；**未声明的跨 backend 依赖直接拒绝**（D19/D26）。
- `StepContext{session, document, child*, last}`：落 `pipeline/` 或 `backend/`——`contract` 不得 include `terminal`（include_firewall_test.cpp:52-53）。
- 原语类型**唯一权威 = b1b2 §B1.1**（`OpSpec`/`OpRegistry`；`OpPolicy` **已删**）；执行顺序由 `run_plan(StepContext&)` 展开（无虚表）。
- **插入点**：`state_from` 之后、`Backend::run` 之前（`pipeline.hpp:71-83`）；被拒 plan **不触碰攻击路径**。
- **去重**：`selection_supported`（**`main.cpp:184`**）与 `combination_available` 并入同一次 `plan_gate`。
- **沿革（D22 已关闭）**：条件表达式原为「受限文法 vs Lua」二选一；2026-10-07 裁决**采用 Lua 5.4**（见 [ADR-0008](adr/0008-lua-runtime-sandbox.md)），受限文法不再作为运行期方案。

## 6. 破坏面清单

- **生产**：`steps.cpp`（seccomp :242-262；`probe.leaf` :215；`park` :111-127；`drain` :86/:240/:382/:417）、`root_child.cpp:41-104`、`backend_profile.cpp:55-61`、`backend_terminal.cpp:502-508`。
- **契约**：`step_plan.hpp:88-105`、`model.hpp:302-306`、`profile/schema.hpp:282-304`、`route_status.h:9-15`。
- **测试**：`step_plan_test.cpp`、`queue_wire_test.cpp:430-441`、`stepset_steps_manifest_test.cpp`、`component_catalog_test.cpp:155-176`。
- **文档**：plan（§3、§10）、decisions、本文。
