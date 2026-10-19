# 附件：P4 `plan_gate` 优先级与里程碑 B 登记

> 分析类（≤8192 B）。**配套**：[P4 规格](../archive/20261017-1835-composition-layer-p4-spec.md) §3（指针指向本件）· [rewrite-plan](../archive/20261017-1835-rewrite-plan.md) §8.1（偏差台账）。判据形式 = D1。

## §A1 `plan_gate` 判定优先级（**单一权威表**；测试按表断言，**不改代码顺序**）

| 序 | 判定点 | 类别 | 备注 |
|---|---|---|---|
| 1 | `plan-missing` | **计划层** | 无可执行计划（含 op 列表为空；见 §A2） |
| 2 | `queue-removed` | **计划层** | `backend.<id>.queue` 出现即拒（R11） |
| 3 | `geometry-missing` | **全局事实** | 3–5 同为全局事实，按此序 |
| 4 | `capability-missing` | 全局事实 | 事实来源 = `contract::Capabilities`（只读） |
| 5 | `executor-unavailable` | 全局事实 | 构建期四选一的结果 |
| 6 | `unknown-op` | **逐 op** | 6–12 为逐 op 扫描序 |
| 7 | `hash-mismatch` | 逐 op | |
| 8 | `op-not-available` | 逐 op | `window.w1w3_umh` 即此条的可测载体（见 §A3） |
| 9 | `route-not-available` | 逐 op | |
| 10 | `order-violation` | 逐 op | landing 只能在操作之间 |
| 11 | `param-invalid` | 逐 op | 上界 = `ParamSpec.max` |
| 12 | `landing-in-pi-window` | 逐 op | 运行期观测到窗口内调用 |

- **裁定（Lead 2026-10-08）**：**以实现的优先级为准并写入规格**（即本表）；**不改代码顺序**。
- **语义**：同一次 gate 调用**按表顺序**返回**第一个**命中项；`path`/`index` 指向该命中点（语法见 P4 规格 §3）。

## §A2 空计划语义（D-4）

- **裁定**：`plan-missing` **覆盖「无可执行计划」，含 op 列表为空**（空脚本、脚本返回空表、脚本缺件三种情形同判）。
- **极端值用例口径**：`op 列表为空` ⇒ **必须** `plan-missing`（**不得**返回 ok，**不得**退化为「无操作的计划合法」）。

## §A3 注册表第 4 条 op（D-5）

- **新增**：`window.w1w3_umh`，**`available=false`（planned）**。
- **理由**：让 `op-not-available` **可达且可测**；与 catalogue 的 `available=false` 组合同构（解析接受、门禁拒绝、UI 置灰）。
- **边界**：**不改变**已接线组合的判定结果（既有 op 的可用性不变）。

## §A4 D-6（保持）

- **D-1（R1 层表登记挂账）保持**：owner = native-core，时机 = R1 批次（防火墙测试恢复时同批）；详见 [rewrite-plan](../archive/20261017-1835-rewrite-plan.md) §8.1。

## §A5 D-7：`contract::StepExecution` 的取代关系只在新层注释里划掉

- **事实**：`StepExecution` 的**取代关系**（由 `OpSpec` + `OpRegistry` 取代）**仅在新层注释**中标注为划掉；**兼容面副本未动**（以保持 `compat-check` **97/0**）。
- **归并**：本条并入 **U-4（逐符号最小化）** 的说明 —— 兼容面是**保形逐字复制**，其「划掉项」的物理清理属**最小化**动作，须在 P4/P5 明确新层所需后逐头处理（判据与时机同 U-4）。

## §A6 已知限制：`plan_gate` 的输入形态

- **现状**：`plan_gate` 输入为**中性输入 + 事实布尔**（`DeviceFacts` 形态），**真实接线**（会话/探测/执行器的事实来源）属 **P3/P5**。
- **登记**：以上为**已知限制**；接线时机 = **P3 会话/配置层 + P5 执行器面**；接线时**不得**在 gate 内自行探测（P4 规格 §3 已定：事实由组合根填充并冻结、gate 只读）。
