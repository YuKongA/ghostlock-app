# Kotlin 面：handoff plan / 脚本通道 / gate 镜像（设计）

> 分析类（≤8 KB）。配套 [plan](handoff-payload-plan.md)、[b1b2](handoff-payload-b1b2-runtime.md)、[ops](handoff-payload-ops.md)、[**kotlin-io**](handoff-payload-kotlin-io.md)（编解码/导入通道/strings）、[plugin-lua](handoff-plugin-lua.md)。**自包含，不引用任何外部计划**。

## 1. 类型与签名

```kotlin
data class HandoffIntent(val backendToken: String?, val items: List<PlanItem>,
                           val executor: ExecutorKind,   // 构建期四选一（单值）
                           val scripts: List<ScriptRef>, val modules: List<ModuleRef>)
sealed interface PlanOutcome {
    data class Built(val plan: HandoffPlan) : PlanOutcome
    data class Rejected(val reasons: List<GateReason>, val path: String?, val index: Int?) : PlanOutcome
}
class HandoffPlanBuilder(private val gate: GateMirror, private val store: ScriptStore) {
    fun build(intent: HandoffIntent): PlanOutcome
}
class GateMirror(private val snapshot: KernelSnapshot)   // K-7：快照来源见 §1

data class HandoffPlan(val tree: ProfileTree, val scripts: List<ScriptRef>,
                       val trust: List<ScriptTrustView>)
data class PlanItem(val backend: String?, val op: String, val route: String?,
                    val seam: String?, val stage: String?,
                    val always: Boolean, val attempts: Int)
data class ScriptRef(val id: String, val sha256: String, val bytes: ByteArray)
data class ModuleRef(val path: String, val sha256: String, val requireBypass: Boolean)
data class ScriptTrustView(val author: String?, val source: String?, val sha256: String,
                           val size: Int, val primitives: List<String>, val caps: List<String>,
                           val budget: String?)
```

- **失败语义**：`build()` **不抛异常**；不可满足 ⇒ `PlanOutcome.Rejected(reasons, path, index)`（与 native gate 同名同义、同序）。
- **`GateMirror` 快照来源（K-7）**：沿用 `kernelSnapshot` 先例（`GhostlockViewModel.kt:152`，消费 `:209`/`:265`）；不新增探测。
- `GateMirror` 只做**预检镜像**；权威 = native `plan_gate`（b1b2 §B1.2）。

## 2. sections 多 owner（R8 相容）

- `ProfileTree.sections: Map<Owner, Section>`——owner 是键；adapter **循环**遍历 owner，不取「第一个 backend」。
- **Builder 不再「单选中 owner」**：`add_*` 每次显式给段名或走根段；装配期校验「段 ∈ 已声明 owner 集合」；与 **D17**（按 plan 用到的 backend 集合发射）一致。
- **根 backend token 可缺省**：`backendToken == null` ⇒ 取文档根 backend（D16）；条目自带 `backend` 时以条目为准。

## 3. 队列元素 schema（R1/R2 裁决）

```hocon
queue = [
  { backend = "cve_2026_43499", op = "write.selinux", route = "tcp_zerocopy" },
  { op = "write.cred", route = "tcp_zerocopy" },   # backend 缺省 ⇒ 根 backend
  { backend = "cve_2026_43499", seam = "plugin", stage = "init" },    # 只在条目之间
  { backend = "cve_2026_43499", op = "write.seccomp.flags", route = "select_stack" },
  { backend = "cve_2026_43499", op = "write.seccomp.mode", route = "select_stack", attempts = 2 }
]
```

| 字段 | 类型 | 规则 |
|---|---|---|
| `backend` | str? | 缺省 = 根 backend；必须 ∈ 已声明 owner |
| `op` | str | ∈ `OpRegistry`（b1b2 §B1.1）；**字面量/常量表**（D28 约定，非门禁） |
| `route` | str? | ⊆ 该 op 的 routes；缺省 = op 默认 route |
| `seam` / `stage` | str? | **只在操作之间**；`seam` 取值即标识（合法值 `plugin`）；`stage` ∈ 合法集合 |
| `always` | bool? | **条目属性**；仅脚本条目可用 |
| `attempts` | int? | ≥1；缺省取 op 默认（D5） |

- **PL 不变仅限 params**（不为参数加白名单键）；**payload 键需在解冻批次为两个 `Backend*Keys` 增 `payload` + 对拍**：触点 `ProfileLayout.kt:182-183`/`:190`（白名单）、`:620`/`:650`（`requireKeys`）、`:342`→`:1037-1038`（fail-closed）；对拍见 [verification](handoff-payload-verification.md)。
- **R2**：既有 manifest 行不变、payload 键按新行补充；成员表权威见 [queue-schema](handoff-payload-queue-schema.md) §3（镜像 + 对拍）。
- 解析触点/成员表/键数上限（**`id` 已删、折进 `seam` 取值**；`kMaxMapMembers`=**12**（合并口径，见 [queue-schema](handoff-payload-queue-schema.md) §2））：`glkv3_parse.cpp:151-202`、`document.hpp:55-98`；细则见 [queue-schema](handoff-payload-queue-schema.md)。
- **R7**：`ProfileValue` 不需要 `Map` 变体（元素只含扁平标量键）。

## 4. 脚本上 wire（内联 + presence-gated + 落盘归 native）

- 脚本正文**内联为 `bin`**（>256 B 必须走 `bin`；单脚本 ≤64 KB；其余键仍拒 `bin`）；**presence-gated**：无脚本时**逐字节与今天一致**。
- **K-9 收敛**：**App 只产字节 + sha256，不落盘**；**native 在攻击前**把脚本体写到 `GHOSTLOCK_HOME` 固定路径（**0600**）后再交执行面。`.sh` 模板通道 = **合成在 App、落盘在 native**。
- **配置快照与脚本落盘分工**：`HoconWriter`/配置快照**仍属 Kotlin**（走既有导出路径）；脚本落盘**只在 native**（跨进程可见性由 native 保证）。

## 5. 导入通道

- `ScriptStore` = App 侧脚本登记（`kind = Lua`，≤64 KB，UTF-8 源码；**不消费外部字节码**）；与既有 **`.sh` 模板**通道并存。
- **与 plugin 的 fail-closed 导入策略显式区分**：plugin 仍「出现即拒」；脚本按 **D27 无限制导入**（L2 只展示不拦截），两者**不共用**「可信来源」判定代码。
- 导入限值、失败归因、`PayloadKind` 并存细节 ⇒ [kotlin-io](handoff-payload-kotlin-io.md) §2。

## 6. gate 镜像（UI 与运行期）

- `enum class GateReason`（**10 条静态原因**，与 b1b2 同名，含 `executor-unavailable`）；**预检置灰**：`GateMirror` 用本机快照预检，不满足的条目置灰并给原因。
- **运行期**：解析 native 结构化 `plan_error`（`reason`/`path`/`index`，语法见 b1b2 §B1.2），把同一条目在 UI 高亮；**UI 置灰 ≠ gate 权威**。
- **L2 展示字段**（作者/来源/sha256/大小/原语清单/能力/预算）与 **strings 登记**（`MessageResIdGuardTest` 同步）⇒ [kotlin-io](handoff-payload-kotlin-io.md) §3。

## 7. 展开器与导出器复用同一实现

- 「意图 → plan」的展开**只有一处实现**（`HandoffPlanBuilder`）；**导出器复用同一实现**产出 `.bin`，禁止第二条序列化路径（防字节漂移）。
- 展开器为**纯函数**（intent + 快照 ⇒ plan），便于对拍与回归。

## 8. 对拍与边界（R4 / K-2 / K-10）

- **golden 口径（R4）**：**无脚本用例逐字节不变（58 份保）**；**含脚本用例新增 golden**（`native-doc-golden-v3.sha256` 58 行无脚本条目，已核 `grep -i script` = 0）。
- **对拍名单（含新增 5 类）**：`FieldLabelsManifestAgreementTest`、`ManifestWidthCrossModuleTest`、`ControllerInternalsTest`、`StepQueueEquivalence`/`StepQueueAssetMigrationTest`、`MergedQueueCarriageTest`/`QueueWireCarriageTest`（profile-core 侧）。
- **预置脚本存放位置（设计）**：`app/src/main/assets/payload/`（当前 assets 仅有 `profile/`）；运行时由 **native** 落盘到 `GHOSTLOCK_HOME`。
- **导出集合边界**：只覆盖 **App 预置脚本**；**用户导入脚本不进** `exportProfiles` 基线集合（避免把用户内容写进冻结物）。

## 9. 词汇对照（ADR-0006 → 执行器）

| ADR-0006 词汇（保留） | 新执行器（D6 口径） | 说明 |
|---|---|---|
| `root_child` | **KernelCmdSet** | root child 执行内核命令集（insmod/节点操作）；**单值映射**（不再同时映 UserRoot） |
| `shizuku` | **UserRoot**（Shizuku UserService 启动） | 用户态 root 路径（**唯一**映射到 UserRoot 的旧词汇） |
| `umh_forward` | **Umh** | UMH 可用时优先 |
| （新增） | **CredGrant** | UMH 不可用时的构建期选择 |
