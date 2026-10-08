# 参数来源：两条既有通道（删 `params_ref`）

> 分析类（≤8 KB）。配套 [queue-schema](handoff-payload-queue-schema.md)、[plan](handoff-payload-plan.md) §3/D16、[b1b2](handoff-payload-b1b2-runtime.md) §B1.1、[kotlin](handoff-payload-kotlin.md) §3、[ops](handoff-payload-ops.md) §3。**本文是「参数从哪来」的唯一权威**。**自包含，不引用任何外部计划**。

## 1. 裁决：删 `params_ref` ⇒ 队列元素 7 键

- **最终键集合（7）**：`{backend, op, route, seam, stage, always, attempts}`（除 `op` 外均可缺省）。
- **`params_ref` 已删除**（~~引用 Owner Schema 参数块名~~ ⇒ 划掉保留沿革）：**参数值不进队列**，也不在队列元素里出现任何引用键。
- **键数上限保持 `kMaxMapMembers` 唯一权威、提到 12**（留头寸；详见 [queue-schema](handoff-payload-queue-schema.md) §2）：**7 键 ⇒ 5 个键头寸**。

## 2. 参数来源 = 两条**既有**通道（不新增任何东西）

| 通道 | 来源 | 校验 | 说明 |
|---|---|---|---|
| ① **静态调参** | **既有 Owner Schema**（如 `backend.<id>.execution.stages.*`） | GLKv3 声明 + manifest 110 行 + width | **权威不变**；调参=改这些既有键的值 |
| ② **动态参数** | **Lua 脚本调用 op 时传表**（`op(token, table)`） | gate 按 `OpSpec`/`ParamSpec` 校验（b1b2 §B2.2/§B1.1） | 只在脚本层存在，**不落 wire 键**、不进 manifest |

- **不新增**：wire 路径族（无 `backend.<id>.params.*`）、manifest 行、Kotlin 白名单键（`ProfileLayout.kt` 的 PL **保持不变**）。

## 3. 为什么删（四条事实，逐条可核）

1. **会被现有 Kotlin 白名单 fail-closed 拒**：`ProfileLayout.kt:182-183`/`:190` 的白名单**没有 `params`** ⇒ 命中 `:342` → `:1037-1038` 的**未知 canonical 键拒绝**路径。
2. **manifest 中不存在该路径族**：110 数据行里**无** `backend.<id>.params.*`；唯一含 `params` 的是 **plugin 动态联合行**（`:117`），与队列参数无关。
3. **与既有承诺互斥**：queue-schema 旧文说参数值「受 **manifest 行** + width 校验」，而 b1b2/batch-plan 承诺「**manifest 语义不变、重导为空 diff**」——两者不能同时成立。
4. **同一事实两处定义**：`params.w1.w1_attempts` 与既有 `execution.stages.w1_attempts` 是**同一事实**，两处定义违反**一处读取**（依据：`engineering-rules.md` **R2** / `design-philosophy.md` §3）。

## 4. 失败语义（并入既有原因码；第 10 条由执行器引入）

- **脚本传参**：键不在 `ParamSpec`、类型/位宽/取值范围不符、超出声明上限 ⇒ `param-invalid`，`path = backend.<id>.op[<index>].params.<key>`。
- **静态调参**：既有绑定路径的失败语义不变（`MissingRequired` / `WidthMismatch` / `UnresolvedToken` 等，b1b2 §B1.2 与 ops §4）。
- **解码层超限**（元素 map >12 键）⇒ 具名 `param-invalid`、`path = <section.key>`（经 parse 通道，见 queue-schema §2）。

## 5. 文档同步（**已落地**，task-54/55）

- [queue-schema](handoff-payload-queue-schema.md)：键集合 8 → **7**、删 `params_ref` 定义节、指针指向本文 ✓
- [plan](handoff-payload-plan.md)：§3 样例与元素形态、§10 D16 改 **7 键**；`:27` 旧形态 → **`{backend, seam, stage}`** ✓
- [kotlin](handoff-payload-kotlin.md)：`PlanItem` 删 `paramsRef`、字段规则表删该行、样例同步；**PL 不变**（无需新增白名单键）。
- [b1b2](handoff-payload-b1b2-runtime.md)：R1 表述改 7 键；`ParamSpec` **仍用于脚本传参校验**；`GateVerdict.path` 语法去掉 `.params_ref`。
- [ops](handoff-payload-ops.md) / [params-dispatch](handoff-payload-params-dispatch.md)：删除对 `params_ref` 的引用，R1 行指向本文。
