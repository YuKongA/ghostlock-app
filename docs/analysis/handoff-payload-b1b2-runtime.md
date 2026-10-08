# B1 + B2：原语注册 / gate / Lua 运行期（设计）

> 自包含；配套 [plan](handoff-payload-plan.md)、[b0](handoff-payload-b0-contract.md)、[ops](handoff-payload-ops.md)。基线：工作树未改动。**类型定义的唯一权威 = 本文件**。

## B1 原语注册与 gate

### B1.1 类型（编译期，落 pipeline）

```
    struct ParamSpec   { key; wire; required; max; };
    struct OpSpec      { OpId id; token; RouteMask routes; span<const ParamSpec> params;
                         CapabilitySet requires; EffectMask effects;
                         span<const HandleKind> produces, consumes; };
    class  OpRegistry  {  // constexpr 表；无运行期注册
      static constexpr span<const OpSpec> ops();
      static constexpr const OpSpec* find(OpId); };
```

- 注册服务：register_ops/params/effects/handles（编译期）。
- 编译期保证：`OpId` 唯一；routes ⊆ backend 可用 route；produces/consumes kind 有定义。
- **取代关系**：本组类型**取代** `contract::StepExecution`（`contract/step_catalog.hpp:20-21` 的步骤注册点）——旧概念**划掉**，fold 归属批次 = **B1**（见 b0 §3）。

### B1.2 gate（`plan_gate`）

```
    struct DeviceFacts { uint32_t kmi; bool selinux_enforcing; bool ksu_present;
                         contract::CapabilitySet caps; };
    struct GateVerdict { bool ok; GateReason reason; std::string_view path; uint32_t index; };
    GateVerdict plan_gate(const profile::Document&, const DeviceFacts&, const OpRegistry&) noexcept;
```

- **只做二值判定**，不产生备选（禁止运行期回退）。
- **10 条静态拒绝原因**（**解码层超限**并入 `param-invalid`；见 [queue-schema](handoff-payload-queue-schema.md) §2）：`unknown-op` / `op-not-available` / `route-not-available` / `geometry-missing` / `capability-missing` / `order-violation` / `hash-mismatch` / `param-invalid` / `seam-in-pi-window` / **`executor-unavailable`**（构建期四选一，运行期无可用执行器 ⇒ 静态拒绝）。（**`dirty-failure` 移出 gate**：它是**运行期终止语义**——写/route 失败且未清理 ⇒ 终止进程、不换路；诊断见 ops §4。）
- **`GateVerdict.path` 语法**：`backend.<id>` → `.op[<index>]`（0 基）→ `.<field>`（`.route`/`.seam.stage`/`.executor`）；`index` 与 `[i]` **必须一致**（用例断言）。
- **插入点**：`state_from` 成功之后、`Backend::run` 之前（需要几何，不能更早）。
- **去重**：orchestrator 的 `selection_supported`（`main.cpp:184` 调用点）与 `combination_available` **并入 gate**；同一事实不得两处判定。
- **`DeviceFacts`**：组合根在探测后、进 pipeline 前填充并冻结；gate 不得自行探测。
- **`capability-missing` 的事实来源**：`contract::Capabilities`（中性 POD，`session/core_session.hpp:34`）——由组合根写入，gate 只读；不得在 gate 内重新探测或推导。

### B1.3 owner 面与队列元素（R1/R2/R8 裁决）

- **Document 允许 owner 集合**；`Builder` 不再「单选中 owner」（`add_*` 显式给段名或走根段）。**与 D17 相容**：wire 段集合 = plan 用到的 backend 集合。
- **R1 队列元素 = 7 键扁平标量**（`id` 已删、折进 `seam`；`params_ref` 已删）；**参数值不进队列**（见 [params](handoff-payload-params.md)）。schema/成员表见 [queue-schema](handoff-payload-queue-schema.md)。
- **R2**：**既有 manifest 行不变；payload 相关键按新行补充**（预期）；成员表权威见 [queue-schema](handoff-payload-queue-schema.md) §3。

## B2 Lua 运行期

### B2.1 集成与沙箱

- **vendored Lua 5.4**（`src/lib/lua/`，mpack 同法，`Makefile:94-105`）；**保留源码解析**（107 KB；攻击前一次）。
- **不注册** io / os / package / debug / load / dofile / require；`math` 只留整数。
- `lua_sethook(L, hook, LUA_MASKCOUNT, N)` ⇒ **指令预算**；`lua_setallocf` ⇒ **内存硬上限**；栈深度限制。
- 单脚本 **64 KB**、sha256 钉；脚本**只在操作之间**运行。

### B2.2 宿主 API（白名单，唯一入口）

```
    op(token, table)        -- 调注册原语；token 必须 ∈ OpRegistry
    probe(name)             -- 只读探针
    handle(kind)            -- 取得类型化句柄（见 B2.3）
    slot.get/set(name, v)   -- 轻量变量：仅 UInt/Int/Bool/Str，有界 64，生命周期 = 一次 run_plan
    require_cap(name)       -- 能力声明（启动前校验用）
```

### B2.3 类型化句柄（D30）

```
    struct Handle { HandleKind kind; uint32_t index; uint32_t generation; };  // 不暴露裸指针
```

- 句柄表**有界 64**；按 kind 校验；`generation` 防悬垂。
- 由**探针**产生；脚本只能传递句柄、**不能伪造地址** ⇒ 写原语不接受裸地址。

### B2.4 校验器（gate 内）与错误语义

- 校验：脚本编译通过；正文 ≤64 KB；sha256 匹配；预算声明存在；`op`/`probe`/`require_cap` 字面量 token 存在（**best-effort 静态扫描，非导入门禁**）。
- **seam = 纯静态**：只能声明在操作之间，操作内部不可表达；运行期观测到窗口内调用即按缺陷（`seam-in-pi-window`）。
- 运行期错误（报错/超预算/无效句柄/越权 API）⇒ **立即终止**（不换路、不回退）。

### B2.5 wire 承载（脚本尺寸唯一权威）

- **脚本正文 >256 B**（`kMaxStringBytes`，`glkv3.hpp:84`）⇒ 必须走 `bin`；**单脚本插入段 ≤64 KB**；其余键仍拒 `bin`（`glkv3_parse.cpp:631-633` 只对脚本键放开）。
- **同批**：`profile_manifest_v3_test.cpp`（`:165`/`:186`）增 `bin` 分支；**既有行不变、新增键按新行补充**；golden 重冻；`bin` 上限入声明表。
- 无脚本时逐字节与今天一致（presence-gated：无 `bin` 键 ⇒ 输出不变）。
- **golden 口径（R4）**：**无脚本用例逐字节不变（58 份保）**；**含脚本用例新增 golden**。已核 `native-doc-golden-v3.sha256` 58 行**无脚本条目**（`grep -i script` = 0）⇒ 全部归「无脚本」，无需逐份点名。

## 层归属（R1）

| 件 | 落层 | 说明 |
|---|---|---|
| OpSpec / OpRegistry / GateVerdict | pipeline | contract 只放概念（b0 §4） |
| plan_gate | pipeline | 需几何/registry；不自行探测 |
| Lua 源码 | `src/lib/lua`（vendor） | 与 mpack 同法（`Makefile:94-105`） |
| 绑定层（宿主 API + 句柄表） | **`script/`（新建，登记为受限层；见 b0 §4）** | 只 include contract/profile/session + `lib/lua`；禁 backend/pipeline/platform/terminal |
| contract | 不含 Lua、不含 terminal 类型 | 空账本保持为空 |

## 判据（黑盒/白盒 + 极端值；见 [verification](handoff-payload-verification.md)）

| 判据 | 极端输入 → 预期 | 目标 |
|---|---|---|
| 预算生效 | 无限循环脚本 ⇒ 超指令预算后**终止** | `native-host-tests`（脚本用例） |
| 沙箱/句柄 | `io.open`/`os.execute` ⇒ 报错终止；伪造 kind/index/generation ⇒ 具名拒绝 | 同上 |
| 硬线 D29 | op 表内不存在块设备/任意路径写 ⇒ 用例断言 op 表无该能力 | 同上 |
| gate 去重 | 两处结论冲突 ⇒ 用例失败（`main.cpp:184` 单一入口） | 同上 |
| bin 放开面 | 非脚本键用 `bin` ⇒ 拒；插入段 >64 KB ⇒ 具名拒 | `profile_manifest_v3_test` |
| 10 条原因穷尽 | `dirty` 不属 gate（运行期终止）；第 11 类 ⇒ 用例失败 | 同上 |

## 批次

> 同批动作（UML 逐图 / ADR 修订 / 需求编号 / 测试对拍 / 验收判据）见 [handoff-payload-batch-plan.md](handoff-payload-batch-plan.md)。

**B1.1** OpSpec/OpRegistry → **B1.2** plan_gate（10 条 + `{ok,reason,path,index}`）→ **B1.3** 去重 → **B2.1** vendor Lua+沙箱 → **B2.2** 宿主 API+句柄 → **B2.3** 校验器+预算 → **B2.4** wire `bin`（manifest/golden 重冻）。
