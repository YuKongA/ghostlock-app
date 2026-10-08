# 参数与分派：三层分工 · 两来源合成 · 编译期策略

> 分析类（≤8 KB）。配套 [b0](handoff-payload-b0-contract.md) §3/§4、[b1b2](handoff-payload-b1b2-runtime.md) §B1.1、[kotlin](handoff-payload-kotlin.md) §3、[ops](handoff-payload-ops.md) §1。**自包含，不引用任何外部计划**。

## 1. 不用父类 / 虚基（三条理由）

1. **禁的不是「虚函数」本身，而是窗口内的间接分派 / 分配 / 首次触碰新页**——判定口径是**窗口边界**，不是语言特性。依据：ADR-0001:183「异常与间接分派绝不进 PI 窗口」、ADR-0004:188-189「不引入虚表：用概念 + 静态 policy（`-fno-rtti`、攻击路径禁间接分派）」、`architecture-findings-register.md:21`（引 ADR-0004 R16）；代码注释 `route_lifecycle.hpp:9-11`、`route_policy.hpp:50`/`:65`/`:191`、`orchestrator.hpp:20`；另 `contract-design.md:1451` 已把「虚函数解禁后」列为需正视的问题。
2. **父类会变成第二权威**：参数塞进共享基类 ⇒ 新增 backend 必须改公共结构，且该类型成为参数事实的**第二处定义**（违反单一权威 + 一处读取）。
3. **contract 不得 include backend**（R1 空账本）：父类若承载 backend 参数，必然把 backend 类型拖进 contract ⇒ 越层。

**一句话原则**：**窗口外允许虚接口（gate / 计划 / 配置构造 / 终端交接）；窗口内只用具体类型 + 静态调用 + POD 状态**（`-fno-rtti` 保持）。
**可操作判据**：① 热路径对象里**不得出现 vtable 符号**（`nm`/`objdump` 或 cmp 归因核）；② 确需多态时**在窗口前完成 vtable 触碰**（warm-up），窗口内不得首次触碰；③ 窗口内**不得分配**、**不得首次触碰新页**；④ 热路径改动 ⇒ **cmp 归因 + 真机门禁**。

### 1.1 虚接口的处置（决定，自 b0 §5 移入）

- `KernelMemory` / `ChildTask` / `AddressDiscovery` / `CapabilityInterface` 一律**不接进 PI 路径**。
- 采用：**(a)** 路径内类型加 `static_assert(!std::is_polymorphic_v<...>)` + 虚接口仅在窗口外使用；**(b)** 后续批次 concept 化（不在 B0）。

## 2. 三层分工

| 层 | 放什么 | 不放什么 |
|---|---|---|
| **中性 op 面**（`contract/`） | op token、**条目侧参数字段**（`WriteEntryParams`）、`WriteOutcome`、诊断码 | route 类型、backend 专有结构、Lua |
| **backend 注册项**（`backend/*/`） | 各自 `OpSpec`（params/能力/副作用/句柄）+ **绑定视图**（`Cve43499OpData` / 43284 视图） | 中性词汇的重定义 |
| **路由策略**（编译期 policy） | `TcpPolicy` / `SelectPolicy` / `MulticastPolicy`（`route_policy.hpp:76`/`:88`/`:102`） | 运行期注册、虚表 |

## 3. 参数两来源在 op 入口合成

```
struct WriteEntryParams {          // 条目侧：脚本可给，经 ParamSpec 校验
    RouteToken route; uint32_t attempts; uint32_t settle_us; /* … */
};
struct Cve43499OpData {            // backend 侧：只读视图，不进 contract
    const profile::TargetProfile* view; uintptr_t target; /* … */
};
template<OpId Id, class Route>
WriteOutcome write_op(OpCtx& ctx, const WriteEntryParams& entry, const Cve43499OpData& data);
```

- **合成点只有一处**（op 入口）：条目值优先，backend 侧只读视图兜底；**两来源都不写对方**。
- 条目侧字段由 `ParamSpec`（b1b2 §B1.1）在 **gate** 完成校验；op 内**不再二次解析/校验**。
- backend 侧结构（`Cve43499OpData`）**不进 contract**：op 实现自己构造并持有，生命周期 = 该次调用。

## 4. 分派方式（条目 route token → switch → 编译期策略）

- **条目 `route` token → 直接 `switch` → 编译期策略**：无虚表、无函数指针、无 `std::visit`。
- 既有范式：`route_policy.hpp:232-246` 的 `run_route()`（`for_each_policy` 直链）；本次把「**profile 单值**」改为「**条目值**」（每条目各自 route，D16）。
- **位置**：分派在 **PI 窗口之外**完成；窗口内只执行已选定的策略。
- 每个策略以 `static_assert(RoutePolicy<...>)` 钉住（`route_policy.hpp:38`/`:140-142`）。

### 4.1 窗口 TU 的禁 include 判据（N9）

- 机制**移出层表**（层表按层匹配，无法表达同层内按文件禁用）⇒ **窗口 TU 清单 + 文件级扫描判据** 见 [queue-schema](handoff-payload-queue-schema.md) §4；承载测试 = `tests/op_tu_isolation_test.cpp`（禁 include 头清单 + `nm` 无 `_ZTV*` + 非法输入 ≠0）。
### 4.2 分派表述（R2 统一）

- **分派在 op 入口、窗口外完成**；**窗口内只执行已选定的编译期策略**（`run_route_policy<P>`）。
- 反例（禁止）：窗口内再选一轮（`for_each_policy`）、窗口内虚调用、窗口内 `std::visit`。

## 5. 43284 对照（跨 backend 只共享中性面）

| 维度 | cve_2026_43499 | cve_2026_43284 |
|---|---|---|
| route | 有（tcp / select / mcast） | **无** |
| 参数来源 | `WriteEntryParams`（条目）+ `Cve43499OpData`（视图） | 自己的 `OpSpec`：**carrier / module / kmi / 等待策略**（`backend_terminal.cpp:119`、`:130-131`、`:257`、`:270`；`glkv3_schema.hpp:31`） |
| 分派 | route token → `switch` → 策略 | 单一策略，**无 route 分支** |
| contract | **一字不改** | **一字不改** |

- **跨 backend 只共享五类**：op token、类型化句柄、数据槽、诊断码、`StageResult`。

## 6. Lua 侧对齐

- 脚本只给 **op token + 表**；native 用 `ParamSpec` 校验后填入**强类型结构**（`WriteEntryParams`）。
- 脚本**不构造 C++ 对象**、**拿不到基类指针**、**拿不到 `Cve43499OpData`**；句柄只能经宿主 API 取得（b1b2 §B2.2/§B2.3）。
- 未知键/类型不符 ⇒ gate 的 `param-invalid`；运行期越权 API ⇒ **立即终止**（脏状态语义、不换路）。

## 7. 例外（仅攻击路径之外）

- **允许**：只在**攻击路径之外**（gate / 计划 / 诊断）使用**闭合 tagged union**——无虚表、静态大小、`switch` 穷举 + 编译期 `static_assert` 覆盖全部 tag。
- **禁止**：攻击路径内一律不得出现虚基、基类指针调用、函数指针、`std::function`（b0 §3 禁令）。

## 8. 队列元素与解析触点（R1/R2 裁决）

- **元素 = 扁平标量键（7 键）**：`{backend, op, route?, seam?, stage?, always?, attempts?}`；**参数值不进队列**（`params_ref` **已删**）——参数走两条既有通道，见 [params](handoff-payload-params.md)。
- **解析触点（native）**：`glkv3_parse.cpp:151-154`（声明数组物化，元素**只收标量成员**，非标量成员记为 unsupported 并具名）、`:277-291`（现把 `params`/`route` 记为**存在性标记** ⇒ 改为 **`route` 文本键**；`params` 标记随 `params_ref` 删除一并移除）；**成员表权威见 [queue-schema](handoff-payload-queue-schema.md) §3**（`backend.cve_2026_43499`/`backend.cve_2026_43284` 各一 `queue`）。
- **R2：不改 manifest 语义**；成员表权威**同上，见 [queue-schema](handoff-payload-queue-schema.md) §3**（+ 镜像与对拍）。

## 9. 承载测试（N14 点名）

| 判据 | 测试文件（新，落 `src/core/tests/`） |
|---|---|
| op TU 隔离（不含具体 route 头、只经 contract/原语） | `tests/op_tu_isolation_test.cpp` |
| gate 判据（**10 条** + `dirty-failure` 走运行期终止） | `tests/plan_gate_test.cpp` |
| 防火墙 0 边 + 层表登记（pipeline/script） | `tests/include_firewall_test.cpp`（既有，扩展层表） |

- 三者的判据形式 = **黑盒/白盒 + 极端输入值**（见 [verification](handoff-payload-verification.md) §1）；`kWhitelist` 保持为空。
