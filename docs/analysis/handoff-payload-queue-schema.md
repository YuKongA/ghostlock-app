# 队列元素 schema · 成员表 · 窗口 TU 判据（第四轮修订细则）

> 分析类（≤8 KB）。配套 [plan](handoff-payload-plan.md)、[b0](handoff-payload-b0-contract.md)、[b1b2](handoff-payload-b1b2-runtime.md)、[kotlin](handoff-payload-kotlin.md)、[**params**](handoff-payload-params.md)、[params-dispatch](handoff-payload-params-dispatch.md)、[batch-plan](handoff-payload-batch-plan.md)。**本文是「队列元素 schema / 成员表 / 窗口 TU 判据」的唯一权威**。**自包含，不引用任何外部计划**。
> **代码引用约定**：`document.hpp`/`glkv3_*.hpp`/`glkv3_parse.cpp`/`schema.hpp` ⇒ `src/core/profile/`；`step_plan.hpp`/`capability*.hpp` ⇒ `src/core/contract/`；`route_lifecycle.hpp`/`primitives.cpp`/`capability_adapters.hpp` ⇒ `src/core/backend/cve_2026_43499/`。

## 1. 元素键集合（7 键）

```hocon
queue = [
  { backend = "cve_2026_43499", op = "write.selinux", route = "tcp_zerocopy" },
  { backend = "cve_2026_43499", seam = "plugin", stage = "init" },   # 合法 seam 只有 "plugin"
  { backend = "cve_2026_43499", op = "write.seccomp.mode", route = "select_stack", attempts = 2 }
]
```

- **最终键集合（7）**：`{backend, op, route, seam, stage, always, attempts}`（除 `op` 外均可缺省）。
- **删 `id` / `params_ref`**：接缝标识折进 `seam`（`contract/step_plan.hpp:159-161`：**合法 seam 只有 `"plugin"`**；`vrko` 只是**旧 id 举例**，已随 `id` 删除）⇒ 键数 9 → **7**（与 §2 头寸配套）。
- **定稿**：`D16 = {backend, op, route?, seam?, stage?, always?, attempts?}（7 键）`；~~`params_ref?`/`params?`/`seam={seam,id,stage}`~~ **已划掉**。
- **参数值不进队列**（`params_ref` **已删**）：参数走**两条既有通道**，见 [params](handoff-payload-params.md)。

## 2. 键数上限：合并为唯一权威 `kMaxMapMembers` = 12（含解码层具名）

- **事实**：① 解码层 `glkv3.hpp:95` `kMaxMapMembers = 8u`（用于 `glkv3.cpp:139` `mpack_expect_map_max`）；② 物化层 `document.hpp:56` `kMaxCompositeKeys = 8U`（用于 `glkv3_parse.cpp:168`）——同一件事的两个名字。
- **只改后者不产生头寸**（合并前）：>8 键的元素 map 在**解码层**就 TypeMismatch 失败，**永远到不了** `glkv3_parse.cpp:168` 的具名分支。合并后 **>12 键**在解码层失败，`reason = param-invalid`、`path = <section.key>`（同 §2 末条）。
- **裁决**：**保留 `kMaxMapMembers` 为唯一 wire 级上限并提到 12**；**删除 `kMaxCompositeKeys`**（唯一使用点 `glkv3_parse.cpp:168` 改读它）；若保留该名则 `= kMaxMapMembers` + `static_assert`（**优先删除**）。
- **解码层失败也必须具名**：`mpack_expect_map_max` 触顶 ⇒ 不得以 TypeMismatch 结案；`reason = param-invalid`（并入既有原因码，**不因参数新增**；第 10 条由**执行器**引入 `executor-unavailable`）、`path = <section.key>`，经 **parse 错误通道**具名上报。
- **保留**：`kMaxCompositeItems = 64`、`kMaxCompositeTextBytes = kMaxStringBytes`。
- **pre-gate vs gate 分工**：`state_from` 的**绑定校验**（声明/类型/必需键）= **pre-gate**（不产出 gate 原因）；`plan_gate` 只做**静态策略判定**（10 条，含 `executor-unavailable`）；同时失败 ⇒ 先报 pre-gate。
## 3. 成员表落地（`DeclaredArrayField` 增 `members`）

- **现状**：`document.hpp:82-85` 的 `DeclaredArrayField` 只有 `{section, key}`——只声明「哪些键是数组」，**不含成员名单**。
- **扩展为**：`{section, key, members}`，`members` = **`constexpr span<const string_view>`**（7 键，顺序同 §1）；新增 `declared_array_members(section,key)`。
- **两侧共用同一份名单**：native 侧 = `kDeclaredArrayFields`（结构 `document.hpp:82-85`、表 `:87-98`）；**Kotlin 镜像点名 = `profile-core/src/main/kotlin/com/ghostlock/app/data/profile/NativeProfileGlkv3Adapter.kt`（:28）** 内新增同名单常量表，两侧**逐项同序**。
- **新增对拍测试（白盒）**：`QueueElementShapeTest`（native 侧，放 `src/core/tests/`） + `QueueElementShapeAgreementTest`（Kotlin 侧，命名沿用既有对拍风格：`FieldLabelsManifestAgreementTest` / `ManifestWidthCrossModuleTest`）——断言两侧成员名单**逐项同名同序**；极端输入（改序/改名/缺项）必失败。
- **不改 manifest 语义**（R2）：成员表**不**进 manifest 行；一致性靠上述对拍。

## 4. 窗口 TU 的禁 include 机制（移出层表，文件级承载）

- **为什么移出**：`include_firewall_test.cpp:43-47` 的 `Rule{source_layer, forbidden_layers}` **按层匹配**，**无法表达同层内按文件禁用**。
- **窗口 TU 清单（按入口链 `attack_write` → `run_route` → `run_route_lifecycle` → race 推导）**：
  1. `backend/cve_2026_43499/primitives.cpp`（`attack_write` 定义，:146）
  2. `backend/cve_2026_43499/route/tcp_zerocopy_route.cpp`（窗口内 `run_route_lifecycle`，:200）
  3. `backend/cve_2026_43499/route/select_stack_route.cpp`（:516）
  4. `backend/cve_2026_43499/route/multicast_waiter_route.cpp`
  5. `backend/cve_2026_43499/route/route_controller.cpp` / `route_middleware.cpp`（策略支撑，随窗口调用）
  6. `race/pi_race.cpp` / `race/threads.cpp`
  7. `backend/cve_2026_43499/spray.cpp`（**已核实**）：`route/select_stack_route.cpp:435`、`route/multicast_waiter_route.cpp:177-189` 在路由过程内直接调 `spray::prepare/stash/activate/discard_prebuilt_page` ⇒ 属窗口可达面。**传递可达也纳入扫描**：`spray.cpp → memory/{heap_context.h,target.h}`、`leak/address_discovery.h`、`route/route_policy.hpp`（只扫直接 include 会漏面）。
- **判据承载 = `src/core/tests/op_tu_isolation_test.cpp`（文件级扫描）**：① 逐 TU 扫**禁 include 头清单**（含虚基的头）；② `nm` 检查热路径对象**无 `_ZTV*`**；③ 非法构造（给 TU 加禁 include）⇒ **退出码 ≠0**。
- **层表职责回收**：`include_firewall_test.cpp` 只保留**层间**规则（+ `pipeline`/`script` 两项，见 b0 §4）；**同层内按文件禁用**由 `op_tu_isolation_test.cpp` 承担。
> 判据形式见 [verification](handoff-payload-verification.md) §1。

## 5. 参数来源（两条既有通道）

> 参数值**不进队列**（`params_ref` **已删**）：① **静态调参** = 既有 Owner Schema（如 `backend.<id>.execution.stages.*`，**权威不变**）；② **动态参数** = Lua 脚本调 op 时传表，gate 按 `OpSpec`/`ParamSpec` 校验。
> 删 `params_ref` 的四条理由（白名单 fail-closed / manifest 无该路径族 / 与「重导空 diff」互斥 / 同一事实两处定义）、失败语义与文档同步：见 [handoff-payload-params.md](handoff-payload-params.md)。
## 6. 数据槽与扁平 schema 的关系（D20 对齐，必改 a）

- **handoff 数据槽不进队列元素**（R1 = 7 键扁平标量，元素里没有 `produces`/`consumes`）：数据槽改为 **op 级声明**——`OpSpec.produces`/`OpSpec.consumes`（b1b2 §B1.1）+ 生命周期/类型，或由**脚本 `slot.get/set` API** 承担（b1b2 §B2.2）。
- **gate 静态校验**对象改为「**op 级**数据槽声明」；未声明即拒绝（`order-violation`/`param-invalid`，`path` 指向 op/条目）。
- 队列元素**不含任何复合成员**；参数见 [params](handoff-payload-params.md)。

## 7. 破坏面与文档同步（本轮）

- **代码触点**：`document.hpp:55-98`、`glkv3_parse.cpp:151-202`（含 `:168` 具名诊断）、`include_firewall_test.cpp:43-47`（职责回收）。
- **Kotlin 触点**：`NativeProfileGlkv3Adapter.kt`（成员名单镜像）、`app/src/test/kotlin/com/ghostlock/app/ui/FieldLabelsManifestAgreementTest.kt:19` 与 `app/src/test/kotlin/com/ghostlock/app/data/ManifestWidthCrossModuleTest.kt:23`（对拍面扩展：新增 `QueueElementShapeAgreementTest`）。
- **文档同步（第五–八轮累计）**：plan §10 D16/§3、b0 §3/§6/§7、b1b2 §B1.3、kotlin §3、params-dispatch §4.1/§8、batch-plan §4；**未落地项按 batch-plan 待办跟踪**。
