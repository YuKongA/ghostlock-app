# 全流程 UML（**权威**）· 新架构（A0–A6）

<a id="run-current-views"></a>

> **代号与视图入口**：本页混合历次结构，A0–A6 与 §11.10 的旧 D7 属各自历史计划；Mermaid 内 D1/R1 等仅为节点变量。当前视图按[主计划语义映射](../plan/backend-root-execution-unification-plan.md#plan-semantic-identifiers)定位：[P-RUN-IO-001/批次01：脚本](#run-view-r2)、[P-RUN-X-001/批次01：执行主干](#run-view-r3)、[P-LOG-X-001/批次01：日志中枢](#run-view-r4c)、[P-BACKEND-X-001/来源S1：既有 common source](#run-view-r5)。原历史、状态和取代关系不变，来源S1不授权接回 carrier；wire 字段仍见 [P-RUN-IPC-001](../plan/backend-root-execution-runhost-wire.md#p-run-ipc-001)。

> 2026-10-23 JNI v4 已获用户“按照计划”批准：§11 取代旧的 route/JNI handle/Flow/Finish 局部图。

> **本文件是唯一权威 UML**（`docs/development/full-process-uml.md`）；旧的旧实现 UML 已归档为 [old-full-process-uml.md](old-full-process-uml.md)（**仅历史对照**，结构与本件不同）。
> 依据：`rewrite-plan-v2`（A0–A6）· `lua-surface-design`（暴露面 7 条）· `lua-surface-annex-document/-target/-route-switch` · ADR-0008/0009/0010 · `A6-baseline-approval`。日期 2026-10-08。

## 1. IPO（端到端，数据级）

```mermaid
flowchart LR
  K["Kotlin resolve"] --> D1["① 数据面\nHOCON 合并（builtin/imported/overrides）"]
  K --> D2["② 计划\nLua 文本 + 大小 + sha256"]
  K --> D3["③ 收尾 payload\n执行脚本 / LKM（bin + sha256）"]
  D1 --> W["wire（GLKv3 文档 + payload 承载）"]
  D2 --> W
  D3 --> W
  W --> V["native 校验\nschema/字段/sha256/大小"]
  V --> S["stage（**窗口外**·一次）"]
  S --> P["staged 计划（条目序 + 参数）"]
  P --> G["plan_gate 十条 + surface 表校验"]
  G -->|拒绝| R["具名拒绝\nsurface-unknown-name / handle-conflict / param-invalid …"]
  G -->|通过| X["按相位执行"]
  X --> XW["**窗口相位**：writeop / copyop / settle（原子·窗口内零 Lua）"]
  X --> XH["handoff → terminal 相位"]
  XH --> T["terminal 执行 ③ 段 payload\n声明式 on_success / always / on_failure"]
  T --> F["内核兜底（C++：relay/enforce 恢复 + watchdog）"]
```

- **步骤顺序**：**声明顺序与执行顺序由 `static_assert` 绑定**（`contract/step_catalog.hpp` 的 `kStepSetOrders` 三条顺序 + 两个 `static_assert`）。
- **新入口（真机已验证 · A6-R1）**：**文档入 → 计划出 → 如实拒绝** —— `--ghostlock-app-call`（stdin，4 字节大端长度前缀）⇒ 文档解析 ⇒ **计划**（`plan chain=… steps=… source=queue`）⇒ 本树机制未实现 ⇒ **`executor-unavailable` / EXIT=4**（门禁记录：[a6-r1-document-to-plan-20261008-pass](../analysis/device-gates/a6-r1-document-to-plan-20261008-pass.md)）。
- **不变式**：脚本只在 **stage** 期运行（窗口外）；`writeop`/`copyop`/`settle` 由**实现层**在窗口相位执行；**`run` 不在暴露面**（收尾命令属 ③ 段）。

## 2. 状态机（四组）

### 2.1 计划（脚本）生命周期

```mermaid
stateDiagram-v2
  [*] --> 菜单选择
  菜单选择 --> stage: 选定 available 项（description + 脚本）
  stage --> staged: 窗口外一次·零副作用
  staged --> 校验: plan_gate 十条 + surface 表
  校验 --> 已拒绝: 具名拒绝
  校验 --> 执行: 通过（按相位）
  执行 --> 完成: 逐条对应 staged
  执行 --> 终止: staged 不一致 / 脏状态 / 越界
  完成 --> 收尾: terminal 按 ③ 段 payload
  终止 --> 收尾: always + on_failure（payload 同源）
  收尾 --> [*]: 内核兜底（C++）
  已拒绝 --> [*]
```

### 2.2 句柄生命周期（含换路与冲突）

```mermaid
stateDiagram-v2
  [*] --> 无句柄
  无句柄 --> 活跃: backend(id, {route?})（构造·无副作用）
  活跃 --> 活跃: writeop / copyop / settle（逐调用边界校验）
  活跃 --> 已释放: close(h)（generation 递增）
  已释放 --> 陈旧: 再用旧句柄 ⇒ handle-stale
  已释放 --> 无句柄: 可重新 acquire（换路 = close + acquire）
  活跃 --> 冲突: 同 backend 第二个活跃句柄 ⇒ handle-conflict
  执行结束 --> 自动释放: 计划结束（兜底）
```

### 2.3 窗口状态机（PI 窗口）

```mermaid
stateDiagram-v2
  [*] --> 窗口关
  窗口关 --> 已武装: prepare（实现层·窗口外）
  已武装 --> 写入闸: execute（进入窗口）
  写入闸 --> 窗口关: disarm
  写入闸 --> 回收: 退出路径（成功/失败/取消/提前返回）
  回收 --> 窗口关: RAII 逐路径回收
  窗口关 --> [*]
```

- **规则**：两条 route **不得同时在窗口内**（串行：先 disarm 再 prepare）；**窗口内零 Lua**。

### 2.4 terminal 状态机

```mermaid
stateDiagram-v2
  [*] --> 待接管
  待接管 --> 已接管: root 后（root_child / umh_forward）
  已接管 --> 执行payload: 按 ③ 段 payload
  执行payload --> 成功收尾: on_success
  执行payload --> 失败收尾: on_failure
  成功收尾 --> 恒收尾: always
  失败收尾 --> 恒收尾: always
  恒收尾 --> [*]: 内核兜底（C++）
```

### 2.5 route 生命周期（**五态四步** · `backend/cve_2026_43499/route/lifecycle.hpp`）

```mermaid
stateDiagram-v2
  [*] --> Idle
  Idle --> Prepared: prepare
  Prepared --> Running: execute
  Running --> Disarmed: disarm
  Disarmed --> Destroyed: destroy
  Idle --> Destroyed: destroy（从任何状态合法且幂等）
  Prepared --> Destroyed: destroy（同上）
  Running --> Destroyed: destroy（同上）
  Destroyed --> [*]
```

- **类型**：`enum class RouteStage`（Idle · Prepared · Running · Disarmed · Destroyed）· `enum class RouteStep`（prepare · execute · disarm · destroy）· `enum class LifecycleVerdict`（拒绝经 `status` 汇报）。
- **边注**：① **拒绝不改状态**（非法步序 ⇒ 拒绝并保持当前态）；② **`destroy` 从任何状态合法且幂等**。

## 3. Class（新树 **9 层** + 资源面 + 暴露面映射）

```mermaid
classDiagram
  class Kotlin {
    +resolve() Document 三段
    +available[] description + 脚本
    +payload 脚本/LKM/sha256
  }
  class Wire { GLKv3 文档 + payload 承载 }
  class NativeEntry { 校验 · stage · gate · 派发 }
  class ScriptSandbox { 注册面 == surface 表 · 预算 }
  class SurfaceTable { 唯一权威 · 7 条 + cfg }
  class Contract { kind/词汇/身份 }
  class CompositionRoot { 句柄/宿主所有者 · 组合固定 }
  class Backend { 步骤 + route（prepare→execute→disarm→destroy） }
  class Terminal { 接管 · 声明式收尾 }
  class Memory { waiter/PI 资源 · RAII }
  class Support { 中性工具 }
  class Profile { 数据面取值 }
  class RuntimeTime { 基址/KASLR 换算 }
  class Rust { extractor：boot/OTA → conf/json }
  class AttackWindow {
    窗口相位宿主 · RAII 回收
  }
  class OpRegistry {
    条目 → 实现登记（与 surface 表同源）
  }
  class ScriptSurface {
    暴露面 7 条唯一权威
  }
  class PlanGate {
    校验 staged（十条 + surface 表）
  }
  class Document {
    Kotlin resolve 三段产物（数据面/计划/payload）
  }
  Kotlin --> Wire
  Wire --> NativeEntry
  NativeEntry --> ScriptSandbox
  ScriptSandbox --> SurfaceTable
  SurfaceTable --> Contract
  SurfaceTable --> Backend
  SurfaceTable --> Profile
  NativeEntry --> CompositionRoot
  CompositionRoot --> Backend
  CompositionRoot --> Terminal
  Backend --> AttackWindow : 窗口相位（RAII 回收）
  SurfaceTable --> OpRegistry : 条目 → 实现登记
  SurfaceTable --> ScriptSurface : 暴露面 7 条
  NativeEntry --> PlanGate : 校验 staged
  Backend --> Memory
  Backend --> RuntimeTime
  NativeEntry --> Support
  Rust --> Kotlin
```

- **沿革（不删）**：旧树的 **`Pipeline` 类**（组合/分派层）**已删** —— 职责并入 **`CompositionRoot`**（接线）+ **`layer_scope`**（编译期校验）；本行即沿革记录（类图中已无该框）。

### 3.4 新树模块（A6-4 · A3 前置批次；**按 namespace 分组**）

| namespace | 件 | 主要声明（与代码一致） |
|---|---|---|
| **`profile`** | `glkv3_document.{hpp,cpp}` | `Document` · `DocumentVerdict`（`ok`/`document-invalid`/`payload-missing`）· `PayloadEntry` · `parse()` / `payload()` / `is_open()` / `reset()` |
| **`profile`** | `payload_keys.{hpp,cpp}` | 载荷四键（`.bin`/`.sha256`/`.size`/`.kind`）三 kind + `payload_segment` / `payload_path` / `payload_name_is_valid` / `payload_key_is_declared` |
| **`profile`** | `document_pairs.{hpp,cpp}` | `read_section_pairs` · `PairsVerdict` · `PairsResult` |
| **`memory`** | `write_request.hpp` | `WriteRequest` · `WriteMode` · `check_request` · `RequestVerdict` · `write_mode_of` / `write_mode_token` |
| **`memory`** | `write_encoder.{hpp,cpp}` | `WriteArm` · `WaiterFieldOffsets` · `arm_for` · `encode_waiter_arm` · `EncodeVerdict` |
| **`backend::cve_2026_43499::route`** | `lifecycle.hpp` | `RouteStage`（五态）· `RouteStep`（四步）· `LifecycleVerdict`（图见 §2.5） |
| **`contract`** | `step_catalog.hpp` | `StepId` · `StepEffect` · `kStepCatalog` · **`kStepSetOrders` 三条顺序** + **两个 `static_assert`** |

- **既有件核对（A6-4 三件 + profile 两件）**：`terminal/payload.hpp` · `terminal/payload_store.{hpp,cpp}` · `terminal/execution.{hpp,cpp}` · `memory/target_profile.hpp` · `profile/route_catalog.hpp` —— **均已在图/本表中在位** ✓。

### 3.0 新树模块锚点（A0–A6）

| 新树模块 | 层 | 在本件的位置 |
|---|---|---|
| **`AttackWindow`** | 窗口宿主（窗口相位） | §1 窗口相位 · §2.3 窗口状态机 · §4.1 |
| **`OpRegistry`** | 操作面（暴露面条目的实现登记，与 surface 表同源） | §3 SurfaceTable→实现层 · §3.1 |
| **`script_surface`** | 契约层暴露面（**7 条 + `cfg`**） | §3 SurfaceTable · §3.1 |
| **`plan_gate`** | 校验（校验对象 = staged） | §2.1 · §4.1 · §4.3 |
| **`Document` 三段** | Kotlin resolve 产物 | §1 · §4.4 |

- **职责权威 = 类图**（§3 的 class 框）；本表仅为**定位索引**（模块 → 本件章节），**不构成第二处权威**。

### 3.1 暴露面 7 条 → 实现层

| 条目 | 实现层 | 相位 | 备注 |
|---|---|---|---|
| `backend(id, {route?})` | **`backend`**（构造经组合根/目录校验） | 窗口外 | **构造 = 无副作用的选择** |
| `close(h)` | **`session`**（句柄表；构造经组合根/目录校验） | 窗口外 | generation 递增；旧句柄 ⇒ `handle-stale` |
| `b:writeop{target={kind,value}, value, width}` | **`backend`**（route/step 为其内部） | 窗口相位 | **kind = 机制枚举**，value 来自 `cfg` |
| `b:copyop{src,dst={kind,value}, width}` | **`backend`**（route/step 为其内部） | 窗口相位 | 结构复制（宽度 = profile 声明尺寸） |
| `b:settle{us}` | **`backend`**（**2026-10-08 更正**：`race` 不再成层，PI-futex 机制并入 `backend/cve_2026_43499/race/`；代码权威 `contract/script_surface.hpp` 的 `implements_layer`） | 窗口相位 | 参数化（不得 C++ 兜底） |
| `b:handoff{terminal, payload_ref}` | **`terminal`** | 窗口外→terminal | **引用语义**（命令内容属 ③ 段） |
| `cfg`（只读快照） | **`profile`**（快照注入） | stage 前 | 只读；不属句柄面 |

- **口径（实现层）**：**实现层 = `contract::kLayers` 的层名**（**9 层**：`contract` · `support` · `memory` · `backend` · `pipeline` · `terminal` · `script` · `session` · `profile`）；**机器可校验**（`script_surface.hpp` 的 `implements_layer` 与 `:305` 断言）；**不得用描述性短语** —— 「**表达一致**」的一个实例。

#### 3.1.1 manifest 原文（**派生源**；`make -C core surface-manifest` 生成，19 行逐字节）

```tsv
backend	call	backend	pre_window	true	true	false	-	-	-
writeop	handle_method	backend	window	false	true	true	handle	true	18446744073709551615
writeop	handle_method	backend	window	false	true	true	target_kind	true	64
writeop	handle_method	backend	window	false	true	true	target_value	true	18446744073709551615
writeop	handle_method	backend	window	false	true	true	value	true	18446744073709551615
writeop	handle_method	backend	window	false	true	true	width	true	8
copyop	handle_method	backend	window	false	true	true	handle	true	18446744073709551615
copyop	handle_method	backend	window	false	true	true	src_kind	true	64
copyop	handle_method	backend	window	false	true	true	src_value	true	18446744073709551615
copyop	handle_method	backend	window	false	true	true	dst_kind	true	64
copyop	handle_method	backend	window	false	true	true	dst_value	true	18446744073709551615
copyop	handle_method	backend	window	false	true	true	width	true	8
settle	handle_method	backend	window	false	true	true	handle	true	18446744073709551615
settle	handle_method	backend	window	false	true	true	us	true	1000000
handoff	handle_method	terminal	terminal	true	true	true	handle	true	18446744073709551615
handoff	handle_method	terminal	terminal	true	true	true	terminal	true	256
handoff	handle_method	terminal	terminal	true	true	true	payload_ref	true	256
cfg	value	profile	stage	false	true	false	-	-	-
close	handle_method	session	pre_window	false	true	true	handle	true	18446744073709551615
kernelcall	handle_method	backend	window	false	true	true	handle	true	18446744073709551615
kernelcall	handle_method	backend	window	false	true	true	op	true	64
kernelcall	handle_method	backend	window	false	true	true	target_kind	true	64
kernelcall	handle_method	backend	window	false	true	true	target_value	true	18446744073709551615
kernelcall	handle_method	backend	window	false	true	true	value	true	18446744073709551615
kernelcall	handle_method	backend	window	false	true	true	width	true	8
step	handle_method	backend	window	false	true	true	handle	true	18446744073709551615
step	handle_method	backend	window	false	true	true	name	true	64
log	call	backend	stage	false	true	true	level	true	16
log	call	backend	stage	false	true	true	key	true	256
```

- **断言**：本表与 manifest **逐项一致**（`make -C core surface-doc-check DOC=docs/development/full-process-uml.md` ⇒ **EXIT=0**）；**pin** = `core/src/tests/surface_manifest_test.cpp`（19 行）。
- **口径**：上表（§3.1）为阅读视图；**权威 = manifest**（列 = 条目 · 形态 · 实现层 · **phase** · … · 参数名 · 必填 · 上界）。

### 3.2 拒绝码（**唯一完整表**；其余文档只留指针）

| 码 | 触发 | 来源/备注 |
|---|---|---|
| **`surface-unknown-name`** | **表外名字**（默认拒绝，不做近似匹配） | `script_surface.hpp` 的 `SurfaceLookup::UnknownSymbol` ⇒ ****已对齐**（`script_surface.hpp:322` 返回 `surface-unknown-name`；断言 `lookup_refusal_is_pinned()` `:545-547` 钉住）** |
| `surface-unknown-token` | 目录/terminal token 不在词表 | catalog / surface |
| **`handle-conflict`** | 同 backend **两个活跃句柄** | gate（校验 staged） |
| **`handle-stale`** | 使用**已释放/陈旧**句柄（generation 已递增） | 句柄表 · ADR-0010 |
| `param-invalid` | 参数越界（`us`/`width`/`value`） | 边界校验 |
| `param-missing` | 脚本调用**缺参**（`width` **必填**） | 边界校验 |
| `width-mismatch` | `copyop` 宽度 ≠ profile 声明尺寸 | 边界校验 |
| `geometry-missing` | 数据面缺**几何/偏移** | profile 取值 |
| **`tuning-missing`** | 调参键**缺失**（不兜底，见 [a3-tuning-migration](../analysis/a3-tuning-migration.md) §3） | profile 取值 |
| `cfg-unknown-field` | `cfg` 字段不在 manifest 声明集合 | profile 快照 |
| `budget-exceeded` | 超**指令/内存/规模**预算 | 沙箱 |
| `op-not-available` / `backend-not-available` | 可用性门禁（`available=false`） | gate |
| `executor-unavailable` | terminal 执行器不可用 | gate |
| `payload-missing` | `handoff` 的 `payload_ref` 无交付物/校验失败 | handoff 校验 |
| `plan-missing` | 计划为空（无 staged 产物） | gate |
| **`document-invalid`** | **文档不是本构建读的那种**：`schema != kDocumentSchema(=3)`（`profile/glkv3_document.hpp:24`）· 字节非 MessagePack（解析失败）· **未解析就读取** | `glkv3_document.hpp` 的 `DocumentVerdict::Invalid`（`:35-39`，三值 = `ok`/`document-invalid`/`payload-missing`）；测试 `tests/document_reader_test.cpp` |
| **`unknown-op`**（`plan_gate`） | 未知 op token | plan_gate |
| **`route-not-available`**（`plan_gate`） | 该 route 不可用（未接线/未声明） | plan_gate |
| **`capability-missing`**（`plan_gate`） | 能力缺失（所需能力未授予） | plan_gate |
| **`order-violation`**（`plan_gate`） | 步骤**顺序违规** | plan_gate |
| **`hash-mismatch`**（`plan_gate`） | **冻结哈希不符**（payload/计划冻结值） | plan_gate |
| **`landing-in-pi-window`**（`plan_gate`） | **接缝落在 PI 窗口内**（R1） | plan_gate |
| **`target-kind-invalid`** | `writeop`/`copyop` 的 `kind` 不在机制枚举内 | **脚本/代码侧**（边界校验） |
| **`queue-removed`** | 出现 M5 已退役的 `queue`/token 形态 | **plan_gate** |
| **`plan-syntax`** | 脚本**语法错**（装载/stage 期解析失败） | **装载/stage 期** |
| ~~`run-form-invalid`~~ | **作废**（`run` 已移除；双形态校验不存在） | 沿革保留 |

- **判据（**可失败**）**：『**在用码集合 == 冻结清单**』——① 从**代码**（`script_surface.hpp`/`op_registry.hpp`/gate 实现）提取全部拒绝码字面量；② 与本表逐项比对；③ **多一个或少一个都 FAIL**（多 ⇒ 未登记码；少 ⇒ 表内陈码）。**首次执行前须先完成上表首行的代码改名**（否则必然差一项：`unknown-symbol` vs `surface-unknown-name`）。

- **可执行验收（提取源写死）**：对 **`core/src/contract/script_surface.hpp`** · **`core/src/pipeline/plan_gate.hpp`** · **`core/src/profile/glkv3_document.hpp`**（`DocumentVerdict`：`document-invalid`/`payload-missing`）做「`return` + 引号内字面量」**去重提取**（**兜底串单列**）⇒ 与本表**逐项比对**，**多一少一皆 FAIL**。

- **冻结清单的导出口径（2026-10-08 补）**：冻结集合 = **『代码在用码』∪『设计/plan_gate/装载在用码』** —— **surface 側代码 5 个**（`surface-unknown-name` · **`target-kind-invalid`** · `width-mismatch` · `handle-conflict` · `handle-stale`）；**gate 侧提取源 = `core/src/pipeline/plan_gate.hpp` 的 `gate_reason_name()`（`:47+`，与 `GateReason` `:30-44` 一一对应）** = `ok` · **`unknown-op`** · `op-not-available` · **`route-not-available`** · `geometry-missing` · **`capability-missing`** · **`order-violation`** · **`hash-mismatch`** · `param-invalid` · **`landing-in-pi-window`** · `executor-unavailable` · `queue-removed` · `plan-missing`（**另有兜底串 `unknown`**：正常路径不应出现，**若可触达须单独登记**）；设计/装载侧补 **`queue-removed`**（M5 退役）· **`plan-syntax`**（装载/stage）· `surface-unknown-token` · `param-*` · `geometry-missing` · `tuning-missing` · `cfg-unknown-field` · `budget-exceeded` · `op/backend-not-available` · `executor-unavailable` · `payload-missing` · `plan-missing`；**已作废码单列**（~~`run-form-invalid`~~）。**本表自身即来源登记**，新增码须同时改代码与 `plan_gate` 并登记于此。

- **指针**：条目级拒绝码散见 [lua-surface-design](../archive/20261017-1835-lua-surface-design.md) §1 与附件；**完整集合以本表为准**，其他处不另立清单（「表达一致」）。

### 3.3 Kotlin / Rust 位置

| 件 | 位置 | 职责 |
|---|---|---|
| Kotlin | `app/` + `profile-core/` | HOCON 解析/合并/覆盖 · `resolve`（Document 三段）· payload 生成 |
| Rust | `tools/extract_rs/` | boot.img / OTA → profile（`--format conf` / `json`） |

**Kotlin 能力契约（2026-10-14，接口已编译；backend 尚未接线）**：

```mermaid
classDiagram
  AccessSessionFactory --> AccessSession : opens
  AccessSession o-- KernelMemoryAccess : borrows
  AccessSession o-- PageCacheAccess : borrows
  AccessCapability <|-- KernelMemoryAccess
  AccessCapability <|-- PageCacheAccess
  AccessCapability --> AccessDescriptor : declares
  AccessDescriptor --> OperationLimits : read/write
```

`AccessSession` 在 Kotlin 管生命周期；native backend 仍拥有 native 资源。`AccessDescriptor` 只发布本会话已建立的操作限制，不能由接口存在性推断读写已可用。

```mermaid
stateDiagram-v2
  [*] --> INITIALIZING
  INITIALIZING --> READY: backend 建立能力
  INITIALIZING --> FAILED: 初始化失败
  READY --> FAILED: native 结果不确定 / cleanup DIRTY
  READY --> CLOSED: close / 停止访问后释放
  FAILED --> CLOSED: close / disarm 后释放
```

这张状态图仅描述新增 Kotlin 契约的允许迁移；实现、JNI 时序与设备结果仍由后续批次验证。

### 3.5 资产视图（**B1 起**）

| 类别 | 落点 | 描述什么 | 派生/权威 |
|---|---|---|---|
| **计划脚本资产** | `app/src/main/assets/plan/plan-<backend>-<route>.lua` | **顺序**（一个 (backend, route) 的执行顺序） | 每个**已接线** (backend, route) 一份；集合由 **`combination-manifest.tsv` 运行时副本 + `available=1` 去重派生**；`<route>` = manifest **route 列原值**，无 route 轴者写 **`none`** |
| **profile 资产** | `app/src/main/assets/profile/**` | **设备**（几何/偏移/调参/可用性） | 按设备档（`<uname-r>.conf` + `execution-*.conf` 等） |

- **两类资产分离**：前者描述**顺序**、后者描述**设备**（B1 方案 Q1 裁定）；两者**不得互相承载**（顺序不得藏进 profile、设备事实不得藏进 plan）。
- **当前实例（2026-10-08 实测 · kotlin-app）**：**`plan-cve_2026_43284-none.lua`**（43284 **无 route 轴** ⇒ manifest route 列写 `none`，共 3 行）+ 43499 的 3 条 route ⇒ **计划脚本资产共 4 份**（集合由运行时副本 + `available=1` 去重派生）。

## 4. Sequence（四张）

### 4.1 端到端（选菜单项 → 执行 → 收尾）

```mermaid
sequenceDiagram
  participant U as 用户 / Kotlin
  participant N as native
  participant S as Lua 沙箱
  participant G as plan_gate
  participant B as backend/route
  participant T as terminal
  U->>N: Document（数据面 + 计划 + payload）
  N->>N: 校验（schema/sha256/大小）
  N->>S: stage（窗口外·一次）
  S-->>N: staged 计划
  N->>G: 校验 staged（十条 + surface 表）
  G-->>N: ok / 具名拒绝
  N->>B: 窗口相位执行（writeop/copyop/settle）
  B-->>N: 结果（WriteOutcome）
  N->>T: handoff → terminal 相位（③ 段 payload）
  T-->>N: 声明式收尾结果
  N-->>U: 诊断（内核兜底留 C++）
```

### 4.2 换路（方案 2：close + 重新 acquire）

```mermaid
sequenceDiagram
  participant S as 脚本
  participant N as native
  participant R1 as route A
  participant R2 as route B
  S->>N: close(hA)（generation 递增）
  N->>R1: disarm → destroy（窗口外）
  S->>N: backend(id, {route="B"})（同 backend·新句柄）
  N->>R2: prepare（窗口外）
  S->>N: writeop（走到窗口相位）
  N->>R2: 原子写
  Note over S,N: 无「切换」动词；runtime 永不自己换路
```

### 4.3 句柄冲突与陈旧

```mermaid
sequenceDiagram
  participant S as 脚本
  participant G as gate
  S->>G: 提交 staged（含 hA 与 hB，同 backend）
  G-->>S: **handle-conflict**（两个活跃句柄）
  S->>G: close(hA) 后提交
  G->>G: 逻辑校验通过
  S->>G: 再次使用 hA
  G-->>S: **handle-stale**（generation 已递增）
```

### 4.4 收尾 payload

```mermaid
sequenceDiagram
  participant K as Kotlin resolve
  participant N as native
  participant T as terminal
  K->>N: payload（脚本 + LKM bin + 大小 + sha256）
  N->>N: 校验 sha256/大小 → 落盘（固定路径 0600）
  N->>T: handoff{terminal, payload_ref}
  T->>T: 按声明执行 on_success / always / on_failure
  T-->>N: 结果 + 审计行
  N-->>N: 内核兜底（relay/enforce 恢复 + watchdog）
```

## 4.5 JNI 边界与 Kotlin 步骤（历史；批准后由 §11 取代）

> Round 13 已取代 J1–J7 的 `invoke(op, frame)`：每个 backend 一个 JNI 库，边界只接收普通 JVM
> 字段。D1 只完成参数定义、生成和读取器形状；真实 landing 装配属于 D2，Kotlin 高阶步骤适配属于 D3。

### 4.5.1 Class（边界两侧）

```mermaid
classDiagram
  direction LR
  class AccessSession {
    <<interface>>
    +phase : SessionPhase
    +cleanup : CleanupDisposition
    +kernelMemory : KernelMemoryAccess?
    +pageCache : PageCacheAccess?
    +close()
  }

  class AccessSessionFactory {
    <<interface>>
    +open(request) AccessResult~AccessSession~
  }
  class Native43499 {
    +create() Long
    +initialize(handle, Init43499) Int
    +prepare(handle, Prepare43499) Int
    +discover(handle, Discover43499) Int
    +writeWord(handle, WordWrite43499) Int
    +shutdown(handle) Int
    +destroy(handle) Int
  }
  class Native43284 {
    +create() Long
    +initialize(handle, Init43284) Int
    +resolveTarget(handle, ResolveTarget43284) Int
    +writeBlock(handle, WriteBlock43284) Int
    +waitTerminal(handle, Wait43284) Int
    +shutdown(handle) Int
    +destroy(handle) Int
  }
  class StepEngine {
    +run(plan, inputs) List~StepResult~
  }
  class StepPlans {
    +forStep(step, inputs) Plan?
  }
  class PlanCatalog {
    +declaredSteps(backend) +fromTokens(tokens)
  }
  class StepInputs
  class ContractTable["attack_contract_fields.hpp（手写维护）"]
  class ContractDtos["手写 Kotlin DTO（core/generated）"]
  class ContractReader["read<Group>()（同一张手写表）"]
  class Session43499["native Session43499"]
  class Session43284["native Session43284"]
  class Seams43499["Ops43499Seams (device landing, J3b)"]
  class Seams43284["Ops43284Seams (device landing, J4b)"]

  AccessSessionFactory ..> Native43499 : selects backend
  AccessSessionFactory ..> Native43284 : selects backend
  StepEngine --> StepPlans : one step to call sequence
  StepEngine --> PlanCatalog : declared order
  StepEngine ..> Native43499 : typed high-level calls
  StepEngine ..> Native43284 : typed high-level calls
  ContractTable --> ContractCodegen
  ContractCodegen --> ContractDtos
  ContractCodegen --> ContractManifest
  ContractTable --> ContractReader : preprocessor projection
  ContractDtos --> Native43499
  ContractDtos --> Native43284
  Native43499 --> ContractReader
  Native43284 --> ContractReader
  Native43499 --> Session43499 : opaque handle
  Native43284 --> Session43284 : opaque handle
  Session43499 ..> Seams43499 : D2
  Session43284 ..> Seams43284 : D2
```

### 4.5.2 状态机（会话相位 + 一步的结局）

```mermaid
stateDiagram-v2
  state "native backend session" as N {
    [*] --> created
    created --> initialized : initialize DTO validated
    created --> failed : typed field/range refusal
    initialized --> prepared : 43499 prepare / 43284 target resolve
    prepared --> frozen : effect indeterminate
    created --> closed : shutdown + destroy
    initialized --> closed : shutdown + destroy
    prepared --> closed : shutdown + destroy
    failed --> closed : shutdown + destroy
    frozen --> closed : shutdown + destroy
  }
  state "Kotlin SessionPhase / CleanupDisposition" as K {
    [*] --> INITIALIZING
    INITIALIZING --> READY : first snapshot ok
    READY --> CLEAN : every reply ok
    CLEAN --> DIRTY : any indeterminate reply (no way back)
    INITIALIZING --> CLOSED : close()
    READY --> CLOSED : close()
  }
  state "StepOutcome (per step)" as S {
    [*] --> Ran : reply ok
    Ran --> Refused : reply not ok, no retry left
    Ran --> Frozen : reply indeterminate
    Refused --> [*] : engine stops here
    Frozen --> [*] : remaining steps listed Frozen, no calls
  }
```

### 4.5.3 Sequence（一笔事务端到端 · 与设备门禁同路径）

```mermaid
sequenceDiagram
  participant U as temp UI / PlanRunner
  participant E as StepEngine
  participant K as Native43499 / Native43284
  participant R as generated read Group
  participant N as backend session
  participant D as device landing
  U->>E: run(declaredSteps(backend), inputs)
  E->>E: precondition: inputs complete?
  Note over E: missing value => Refused with zero calls
  E->>K: named function(handle, generated DTO)
  K->>R: read Group(env, dto)
  R->>R: field id/signature/fixed byte length
  R-->>K: typed values or failed field
  K->>N: validate ranges/generations and transact
  N->>D: landing call
  alt landing not wired
    D-->>N: absent
    N-->>K: unsupported + op-not-wired:name
  else landing wired
    D-->>N: result
    N-->>K: code + typed result
  end
  K-->>E: NativeAnswer (indeterminate => DIRTY)
  E-->>U: StepResult (Ran / Refused / Frozen + calls)
```

**门禁命令**：D1 = `./gradlew :nativeTestHost :app:testDebugUnitTest
:nativeBuildAndroid`；真机 =
./gradlew deviceGate（装 APK → 起临时界面 → 抓 logcat → 按 native 导出的 manifest **机器校验**）；
两者合起来 = ./gradlew gateAll。

## 5. Kotlin 侧链驱动器（2026-10-16 · 照 `ghostlock-app-ref@5f184760` 移植）

```mermaid
classDiagram
  class SequenceContinuation {
    -value: Long
    +current(): Long
    +advance(returned: Long)
  }
  class WaitPolicy43284 {
    <<object>>
    +TIMEOUT_MS = 15000
    +POLL_MS = 40
    +POLL_ATTEMPTS = 5
    +ALT_BUDGET_MS = 200
    +await(primary: String, alt: String): Int
  }
  class TriggerSpec43284 {
    <<object>>
    +DEFAULT_DELAY_MS = 500
    +DEFAULT_IGNITER: String
    +DEFAULT_RETRIES = 1
    +detonate(handle: Long, delayMs: Long): Int
  }
  class Assembled43284 {
    +GUARD_POLICY_SKIP = 0
    +hookPlan(symbol, guardPolicy)
    +writeImage(...)
    +restoreHook(plan, width, init)
  }
  Assembled43284 --> SequenceContinuation : advances on success
  Assembled43284 --> TriggerSpec43284 : fires through
  Assembled43284 --> WaitPolicy43284 : waits through
```

要点：`SequenceContinuation` 是**每次运行一个局部对象**（非全局）；三处写点成功后由 native 返回的序号推进，
日志固定 `seq: <prev> -> <next> (auto)`。`WaitPolicy43284` 先判可读标记（`.ghostlock_lkm_ok|fail`），
仍在飞再判 `/dev/dfm0|dfm1`，两者任一命中即终态。`TriggerSpec43284` 的 igniter 与延迟是**参数**，默认 500 ms。
`GUARD_POLICY_SKIP` 值仍为 0（`bti c` 下 Reject 挂不上钩），只是把裸 0 变成有名字的常量并在日志里打出 `guard=Skip`。

参照位置：`real_ops.cpp:250-257/307-327/339-344`（seq 交棒）、`cve_2026_43284_state.hpp:29-31` 与 `real_ops.cpp:405-415`（wait）、
`real_ops.cpp:355-379` 与 `execution_binding.cpp:170`（trigger）、`execution_binding.cpp:241`（guard）。

## 6. 维护约定

- **一处权威**：本文件是唯一权威 UML；其他文档**链接**本件，不重画同一结构。结构变化（新增/删除层、暴露面条目、拒绝码、相位）⇒ **同批**更新本件对应图。
- **引用**：`docs/analysis/adr/README.md` · `handoff-payload-batch-plan.md` · `contract-design.md` · `plugin-runtime-integration-design.md` · `root-manager-selection-design.md` · `docs/archive/*` · `engineering-rules.md` · `AGENTS.md`。
- **两条新纪律（同批留痕）**：本件任何**新条目/新相位/新实现层**必须同时满足 —— ① **无名字→值表**（名字只允许 **dispatch 标识符** 与 **数据面键路径**；见 [target 附件](../analysis/lua-surface-annex-target.md) §C1）· ② **表达一致**（同一件事只允许一种表达，不得「首设 A 语法 / 后续 B 语法」；见 [lua-surface-design](../archive/20261017-1835-lua-surface-design.md) 开头）。
- **沿革（2026-10-08 · 层族 10 → 9）**：**`race` 不再是层** —— `core/src/race/**`（5 文件）整体移入 **`backend/cve_2026_43499/race/`**，命名空间 `ghostlock::race` → `ghostlock::backend::cve_2026_43499::race`；`contract/layer_scope.hpp` 删 `race` 行（断言 10→**9**、7 保持），`settle` 的 `implements_layer` 由 `race` → **`backend`**。**理由 = 纠错**：PI-futex 是 **43499 专属**机制（43284 为 pagecache/UMH，不含 PI-futex），**旧树本就把 futex 机制放在 `backend/cve_2026_43499/leak/futex_hash.h`**；重写时错误地提为公共层，本次与 AGENTS「backend 按 CVE 分子命名空间」对齐。**守卫实证**：删层行而不改断言 ⇒ **四条 static_assert 同时失败**（`kLayerCount == 10` 与 surface 行指向不存在的层）⇒ 修正后才绿。
- **沿革（2026-10-14 · J1-J7）**：旧形状为四个 extern + GLKJ v1 帧；Round 13 已明确取代，历史实现仍在树中，删除归后续独立批次。
- **沿革（2026-10-14 · typed JNI / D1）**：4.5 改为每 backend 一个 JNI 库与具名函数；`attack_contract_fields.def` 单表投影 C++ 读取器、Kotlin DTO、TSV manifest，宿主逐字段哨兵与字段交换负例锁定顺序。D1 不宣称真实 landing 已接线。
- **历史**：旧实现 UML = [old-full-process-uml.md](old-full-process-uml.md)（归档头含理由与恢复来源；登记见 `docs/archive/INDEX-plans.md` §七）。

## 7. Access capability wrapper（历史；批准后由 §11 与 ADR-0022 决策4取代）

```mermaid
sequenceDiagram
  participant UI as LogHub
  participant E as PageCacheEngine
  participant F as JniCapabilitySessionFactory
  participant S as JniCapabilitySession
  participant O as JniPageCacheOps
  participant N as JNI session

  UI->>E: run(context)
  E->>F: open(cve_2026_43284, target)
  F-->>E: AccessSession
  E->>S: initialize()
  S->>N: Initialize
  S->>O: up()
  O->>N: Snapshot / ResolveTarget
  O-->>S: token + width
  S-->>E: capabilities.pageCache
  E-->>UI: capability ready / unavailable
  E->>E: page-cache chain (control plane)
  E->>S: shutdown()
```

要点：描述符只从 native Snapshot 的 flags/width/generation 发布；没有 page-cache 标志时能力为 null；数据面 `PageCacheOps` 走 JNI 的
`ReadBlock`/`WriteBlock`，控制面（hook/trigger/wait/LKM）仍留在 backend 链中。日志经 `EngineLog`：info 无参数，warn/error 一个 reason，
debug/verbose 参数以 `\t` 分隔。
## 8. Kotlin 直写 / DFRoot 路径（2026-10-19）

```mermaid
sequenceDiagram
  participant K as KotlinElfPlanner
  participant W as RawEspWriter
  participant J as NativeRawEsp
  participant L as libc++.so page cache
  participant M as module / UMH
  participant I as init (tid 1)

  K->>K: plan(site=0x6eb54, landing=0x9a160)
  K->>K: DfrootLibcxx 464B + first-inst(0x1C8) + jumpback(0x1CC)
  W->>J: open(spi, encap, sender)
  W->>J: writeImage(landing, 464B)
  J->>L: vmsplice + splice + UDP
  W->>J: writeBlock(site block 0x6eb50)
  J->>L: branch b landing+0x90
  W->>J: trigger orphan
  I-->>L: hit sentry
  L-->>I: DFRoot payload (clone/setcon/insmod)
  I->>M: insmod carrier
  M-->>M: /dev/df + UMH
  W->>J: restore site block
  W->>J: restore 464B stage
  W->>J: close(handle)
```

要点：Kotlin 负责 plan、IV/分块、first-inst/jumpback、恢复顺序；JNI 只做 syscall；页缓存目标由 Kotlin 直接指定，不经过 `PageCacheLanding`。

## 9. 执行层权威图（2026-10-21 · 指针，不复制）

> 本文件 §2.1「计划（脚本）生命周期」、§3 的 `script/` 面、§4.4 handoff 等章节属 **Lua/script 时代**（2026-10-17 已删除，见 MASTER-PLAN §0.1），保留作沿革，**不得作为现行结构依据**。
>
> 执行层的结构与状态机权威图已迁至 **`docs/plan/execution-architecture.md`**：
> - 组件图：`ExecutionEngine → BackendRun → PipelineEngine → node（Cve43499OpNode / Cve43284StageNode）` + 数据面 `AccessSession → capabilities`；
> - 状态机：`SessionPhase`（INITIALIZING/READY/FAILED/CLOSING/CLOSED）与 close/shutdown 互斥；
> - 决策记录：`docs/analysis/adr/0018-execution-authority.md`。
>
> 一处结构只留一处权威图 ⇒ 本文件不再复制这两张图，只保留指针。

## 10. 43499 C 核心会话骨架（历史；批准后由 §11 取代）

> 从属 [43499-43284-cpp-parameterization-plan.md](../archive/20261104-2030-43499-43284-cpp-parameterization-plan.md) P1；P1a 只新增类型/句柄，旧全局仍在。证据 [p1a-session-skeleton.md](../analysis/p1a-session-skeleton.md)。非生产接线。

```mermaid
classDiagram
    class GlkSession {
      +GlkBackend backend
      +union u
    }
    class Glk43499Session {
      +Glk43499Target target
      +Glk43499Config config
      +Glk43499Env env
      +Glk43499Runtime rt
      +Glk43499Result result
      +GlkLogSink log
      +atomic_bool stopping
    }
    class Glk43499Target {
      +uintptr_t self
      +kernel_offsets offsets
      +char release[192]
      +Glk43499Layout layout
    }
    class Glk43499Config
    class Glk43499Runtime
    class Glk43499Result
    class GlkWriteRequest {
      +uintptr_t target
      +uintptr_t value
      +uint32_t mode
      +uint32_t preserve_child
      +const char* label
    }
    GlkSession --> Glk43499Session : u.p43499 (borrowed)
    Glk43499Session --> Glk43499Target
    Glk43499Session --> Glk43499Config
    Glk43499Session --> Glk43499Runtime
    Glk43499Session --> Glk43499Result
```

```text
GlkSession* (caller-owned; no process-wide handle)
caller owns Glk43499Session; destroy never frees it
```

## 11. 43499 Placement / Page / Actor 契约（2026-10-26 header-only）

> 头文件批见 [43499-c11-header-contract-plan](../archive/20261104-2030-43499-c11-header-contract-plan.md)，JNI transport 见 [43499-jni-transport-plan](../archive/20261104-2030-43499-jni-transport-plan.md)。当前 JNI 已接入九入口，但 core 仍是显式 `UNSUPPORTED` 的 unavailable 实现；victim/Finish 不在本批。
> 真实实现的线性所有权权威与批次顺序见 [43499-resource-primitives-implementation-plan](../archive/20261104-2030-43499-resource-primitives-implementation-plan.md)。G4L-0已批准；物理实现仍未开始。

### 11.1 IPO / dependency

```mermaid
flowchart LR
  Lua[FlowLuaImpl + RunScope] --> K[Kotlin route/address/payload]
  UI[UI/default Flow] --> K
  K --> C[general JNI discovery]
  C -->|copied DTO| K
  K --> T[具名 Kotlin DTO]
  T --> B4[43499 JNI 显式逐字段翻译]
  B4 --> U[api_unavailable: UNSUPPORTED]
  K --> B2[43284 JNI]
  G[common STATIC] --> C
  G --> B4
  G --> B2
  K --> F[FinishHandoff + root-manager script]
```

禁止边：general↔backend、backend↔backend。route 仅在 Kotlin/Lua。

### 11.2 Placement / Page / Actor state

```mermaid
stateDiagram-v2
  [*] --> Placement: preparePlacement transaction
  Placement --> Page: landPage success; consume Placement token
  Placement --> Placement: precommit NOT_APPLIED
  Placement --> Closed: postcommit CLEAN_FAILURE; consumed and released
  Placement --> Closed: close before commit
  Placement --> Retained: postcommit DIRTY/INDETERMINATE; consumed

  Page --> Actor: prepareActor success; consume Page token
  Page --> Page: prepareActor clean failure
  Page --> Retained: prepareActor dirty; token consumed
  Page --> Closed: closePage before transfer
  Actor --> Running: executeActor without tag/spec
  Running --> Actor: clean execution completed
  Actor --> Closed: closeActor releases Actor Carrier Page
  Running --> Retained: reference state unknown
  Retained --> [*]: process exit only
```

每个token只有一个Kotlin owner。`placementConsumed=1`时Placement置0；`pageConsumed=1`时Page置0；
仅非零Actor token直接拥有Page与Carrier。raw token误用不提供stale/wrong-kind保护。

### 11.3 Class

```mermaid
classDiagram
  class RunResourceScope
  class NativeGeneralJni
  class Native43499Jni
  class Native43499NamedDtos
  class ApiUnavailable
  class Native43284Jni
  class DiscoveryHandle
  class PlacementHandle
  class PageHandle
  class ActorHandle
  class PrimitiveSpec
  class Carrier
  class AccessView
  RunResourceScope *-- DiscoveryHandle
  RunResourceScope *-- PlacementHandle : current owner only
  RunResourceScope *-- PageHandle : current owner only
  RunResourceScope *-- ActorHandle : current owner only
  NativeGeneralJni --> DiscoveryHandle
  Native43499Jni --> PlacementHandle
  Native43499Jni --> PageHandle
  Native43499Jni --> ActorHandle
  Native43499NamedDtos --> Native43499Jni : explicit fields
  Native43499Jni --> ApiUnavailable : current fail-closed target
  PlacementHandle --> PageHandle : ownership move
  PageHandle --> ActorHandle : ownership move
  ActorHandle *-- PageHandle : owns
  ActorHandle *-- Carrier : owns one bound carrier
  PrimitiveSpec --> Carrier : prepare-time deep copy
  AccessView ..> RunResourceScope : independent public primitive
```

### 11.4 Sequence

```mermaid
sequenceDiagram
  participant K as Kotlin/Lua
  participant C as general JNI
  participant B as 43499 JNI
  K->>C: scan(params)
  C-->>K: copied candidates DTO
  K->>B: preparePlacement(physical request)
  alt current unavailable implementation
    B-->>K: UNSUPPORTED + handle 0; stop
  else future real core
    B-->>K: PlacementHandle + candidate facts
    K->>K: calculate addresses and final page bytes
    K->>B: landPage(placement, bytes, geometry)
    B-->>K: Page token + placementConsumed receipt
    alt placementConsumed = 1
      K->>K: zero Placement; own Page only when token is nonzero
    else placementConsumed = 0
      K->>K: retain Placement for close or retry
    end
    K->>B: prepareActor(page, setup, PrimitiveSpec)
    B-->>K: Actor token + pageConsumed receipt
    alt pageConsumed = 1
      K->>K: zero Page; Actor owns Page only when token is nonzero
    else pageConsumed = 0
      K->>K: retain Page for close or retry
    end
    K->>B: executeActor(actor)
    B-->>K: call status + effect + resource state + receipt
    K->>B: closeActor(actor)
    B-->>K: Actor Carrier Page chain released or retained
  end
```

### 11.5 Implementation gates

```mermaid
flowchart LR
  G0[G4L-0 linear contract] --> G1[G4L-1 Placement]
  G1 --> G2[G4L-2 Landing and Page]
  G2 --> G3[G4L-3 Actor owns Page]
  G3 --> G5[G4L-5 Kotlin Lua wrappers]
  G3 --> GM[G4L-4M Multicast]
  G3 --> GP[G4L-4P Pselect]
  G3 --> GT[G4L-4T TCP zerocopy]
  GM --> ANY{Any supported Primitive}
  GP --> ANY
  GT -. independent approval .-> ANY
  G5 --> G6[G4L-6 victim Finish ksud]
  ANY --> G6
```

G4L-5不等待三个Primitive全部完成；G4L-6至少依赖一个已过真机门的Primitive，TCP仍有独立批准点。
Native不含registry，只绑定物理Primitive；route与跨backend顺序由Kotlin/Lua决定。

### 11.6 2026-10-28：CVE-2026-98260 backend（IPO/状态机）

> 执行结构唯一权威：[execution-architecture.md](../archive/20261104-2030-execution-architecture.md) §10；本图只给 IPO 与状态机（AGENTS §9）。
> 拓扑（2026-10-28 R1 修正）：app Flow（唯一编排）→ Shizuku UserService（shell 域，幂等原语）
> → **公用 victim helper `execScript` 启动脚本 → payload（后台 detach）** → payload 内 **rooted victim
> （fake-cred child）直接 exec finish 脚本**；BootSafetyStore 唯一安全闸。
> 直启 JNI `spawnStatic` 已退出生产路径（TD-R1-5）；详见 `98260-r1-progress-20261028.md`。

```mermaid
flowchart LR
  I["app Flow validate<br/>(profile/release/Shizuku/store/boot_id)"] --> P["prepare<br/>(service 预检 + payload/finish 脚本 + journal)"]
  P --> S["start<br/>(公用 victim helper execScript 启动脚本 → payload 后台/pid)"]
  S --> O["poll<br/>(readLog/marker；app 内超时/取消)"]
  O --> V["verify<br/>(/proc/modules + finish 证据)"]
  V --> F["acceptance<br/>(rooted victim finish 验收；成功也 must_reboot=true)"]
```

```mermaid
stateDiagram-v2
  [*] --> Idle
  Idle --> Running: Gate 通过
  Running --> Finished: Ok（KSU 判据+verify；must_reboot=true）
  Running --> Finished: Failed（未 armed：具名；armed 后：must_reboot=true）
  Running --> Refused: 预检/输入拒绝（未 armed）
  Finished --> Idle: 本 boot 拒绝再跑（store 闸）
  Refused --> Idle
```

### 11.7 2026-10-28：exec-glue backend 选择、43499 profile 与双 process launcher（**历史**；执行通道已被 §11.9 取代）

本节是临时 exec-glue app 路径的唯一结构图：enabled 选择列表由 `BackendSpec.selectable` 决定；43499 配置权威来自上游 `kernel_profiles/index.conf` 或用户导入的自包含 `.conf`，执行前转换成当前 GLK1 wire。

```mermaid
flowchart LR
  UI[Backend selector] -->|selectable=true| GLUED[43499-Glued / 43284Glued]
  UI -. grey, no selection .-> OLD[Original 43499 / 43284 / 98260]
  UI -. grey, no selection .-> LUA[Dynamic Lua]
  GLUED --> SPEC[Concrete BackendSpec]
  SPEC --> GATE[BackendRunGate]
  subgraph ProfileInput[43499 card profile input]
    INDEX[kernel_profiles/index.conf] --> ASSET[Selected built-in .conf]
    IMPORT[SAF imported .conf] --> HOCON[HoconSupport]
    ASSET --> HOCON
    HOCON --> LEGACY[LegacyProfileConverter]
    LEGACY --> VALIDATE[ProfileResolver validation]
    VALIDATE --> WIRE[Profile.toBinary / GLK1]
  end
  WIRE --> FLOW[SequentialFlow: one exec step]
  FLOW --> EXEC[ExecutableBackendProcess: app-owned routing]
  EXEC -->|Shizuku binder reachable + permission granted| REMOTE[shared UserService / shell ProcessBuilder]
  EXEC -->|otherwise| LOCAL[app UID ProcessBuilder]
  REMOTE --> HOST[fixed Glued ID + bounded AIDL input]
  LOCAL --> APPARGS[fixed ELF argv + stdin + run files]
  HOST --> HOSTARGS[fixed ELF argv + stdin + run files]
  APPARGS -. 43284 only .-> APPIPSEC[App-domain IpSecRuntime create → exec → close]
  HOSTARGS -. 43284 only .-> HOSTIPSEC[UserService-domain IpSecRuntime create → exec → close]
```

```mermaid
stateDiagram-v2
  [*] --> NoProfile
  NoProfile --> ValidProfile: card selects built-in or imports .conf; parse + validate
  NoProfile --> NoProfile: Run Gate refuses profile-required
  ValidProfile --> NoProfile: card clears selection
  ValidProfile --> InvalidProfile: persisted source no longer resolves
  InvalidProfile --> ValidProfile: choose a valid built-in/import again
  ValidProfile --> ExecStep: Gate allows; 43499 Flow runs one exec step
```

```mermaid
classDiagram
  class BackendSpec {
    +selectable Boolean
    +displayNameRes Int
    +runner BackendFlowRunner
    +readiness() BackendReadiness
  }
  class Backend43499Glued {
    +profileReference String
    +restoreConfig(store)
    +resolveProfile(context)
  }
  class Backend43284Glued
  class Flow43499Glued
  class Flow43284Glued
  class BackendSelection {
    +select(key) Boolean
    +index Int
  }
  class Glued43499ProfileCodec {
    +builtins(context)
    +resolve(context, reference) Resolved43499Profile
  }
  class ExecutableBackendProcess {
    +run(context, backendKey, binaryName, arguments, stdin, files)
  }
  class ExecutableProcessRouting {
    +select(binderReachable, permissionGranted)
    +dispatch(route, appLauncher, shizukuLauncher)
  }
  class ExecutableRunFileStager {
    +stage(runDirectory, fileName, bytes) StagedRunFileEvidence
    +requireUnchanged(runDirectory, evidence) StagedRunFileEvidence
  }
  class StagedRunFileEvidence {
    +sha256Hex String
    +sizeBytes Long
    +device Long
    +inode Long
    +ownerUid Int
    +permissionBits Int
  }
  class AndroidIpSecRuntime {
    +create(selectedDomainContext)
    +arguments List<String>
    +close()
  }
  class IpSecCliArguments {
    +create(encapPort, senderPort, spi, aesKey)
  }
  class ReverseCloseScope {
    +add(resource, closeAction)
    +close()
  }
  class ExecutableShizukuHost {
    +prepare(config, stdin)
    +start()
    +state()
    +readLog(offset, maxBytes)
    +destroy()
  }
  class ShizukuBackendClient
  class GhostlockUserService
  Backend43499Glued ..|> BackendSpec
  Backend43284Glued ..|> BackendSpec
  BackendSelection --> BackendSpec : only selectable
  Backend43499Glued --> Glued43499ProfileCodec
  Backend43499Glued --> BackendConfigStore
  Backend43499Glued --> BackendRunGate
  Glued43499ProfileCodec --> ProfileResolver
  Glued43499ProfileCodec --> LegacyProfileConverter
  Backend43499Glued --> Flow43499Glued
  Backend43284Glued --> Flow43284Glued
  Flow43499Glued --> ExecutableBackendProcess
  Flow43284Glued --> ExecutableBackendProcess
  ExecutableBackendProcess --> ExecutableProcessRouting : one selected launcher owner
  ExecutableBackendProcess --> ExecutableRunFileStager : App-owned per-run inputs
  ExecutableShizukuHost --> ExecutableRunFileStager : shell-owned per-run inputs
  ExecutableRunFileStager --> StagedRunFileEvidence : identity and content facts
  ExecutableBackendProcess --> AndroidIpSecRuntime : 43284 / App route only
  AndroidIpSecRuntime --> IpSecCliArguments : fixed upstream argv
  AndroidIpSecRuntime --> ReverseCloseScope : transform → SPI → encapsulation
  ExecutableBackendProcess --> ShizukuBackendClient : authorized route only
  ShizukuBackendClient --> GhostlockUserService : fixed Glued ID + bounded data
  GhostlockUserService --> ExecutableShizukuHost : fixed backend allowlist
  ExecutableShizukuHost --> AndroidIpSecRuntime : 43284 adapter / UserService context
```

```mermaid
sequenceDiagram
  participant U as User
  participant P as BackendPage
  participant S as BackendSelection/SessionStore
  participant C as 43499 card
  participant H as HOCON + Legacy converter + ProfileResolver
  participant F as SequentialFlow
  participant E as ExecutableBackendProcess
  participant A as App ProcessBuilder
  participant Z as ShizukuBackendClient / UserService
  participant R as Selected-domain IpSecRuntime (43284 only)
  U->>P: choose 43499-Glued
  P->>S: select(key); disabled spec rejected
  U->>C: select indexed built-in or import .conf
  C->>H: parse, convert, validate
  H-->>C: resolved profile + GLK1 bytes
  C->>S: persist profile reference
  U->>F: Run
  F->>H: resolve persisted reference and encode GLK1
  H-->>F: profile.conf text + profile.bin
  F->>E: one exec step, fixed ELF/argv/stdin/run files
  alt Shizuku binder reachable and permission granted
    E->>Z: prepare fixed ID + bounded data
    Z->>Z: exclusively stage allowlisted files in shell-owned runDir
    Z->>Z: optionally seal supplied 43499 root script 0444; retain file identity evidence
    Z-->>E: prepared evidence
    E->>Z: start/poll only after prepare succeeds
    Z->>Z: 43499 child gets three path envs from shell-owned runDir; 43284 gets none
    opt backend is 43284Glued
      Z->>R: acquire transform/SPI/socket; build fixed argv
      R-->>Z: live args + resource owner
    end
    Z-->>E: terminal exit/log facts
    E->>Z: unbind(remove=true); wait for disconnected callback
    Z-->>E: main-thread callback/cache cleanup drained; next bind may start
  else Shizuku unavailable or unauthorized
    opt backend is 43284Glued
      E->>R: acquire transform/SPI/socket; build fixed argv
      R-->>E: live args + resource owner
    end
    E->>E: exclusively stage allowlisted files in App-private runDir
    E->>E: optionally seal supplied 43499 root script 0444; retain file identity evidence
    E->>A: ProcessBuilder fixed ELF/argv
    A->>A: 43499 child gets three path envs from App-private runDir; 43284 gets none
    A-->>E: terminal exit/log facts
  end
  R->>R: release transform → SPI → encapsulation socket after child terminal
  E-->>U: logs + exit status
```

Shizuku 路由被选定后，prepare/start/连接失败不会启动第二个 App child。本图不宣称 Shizuku-host execution 或 a301so 真机验证完成；这些需单独提供运行与设备证据。
`ExecutableRunFileStager` 的固定 `.ghostlock_root.sh` 文件目前只是 allowlist/staging 能力；当前 43499 Flow 尚未提供该文件，故此图不表示 0444 victim handoff、Finish/Payload 或 detached worker 生命周期已接通。
43499 的两个 ProcessBuilder 只注入 `GHOSTLOCK_HOME`、`TMPDIR`、`GHOSTLOCK_KSU_LOG`；HOME/TMPDIR 指向当前 launcher-owned runDir，KSU log 与 stdout/stderr `run.log` 分离。43284 不注入这些 43499 path overrides。
43284Glued 的 `exp.c`/`elf_parser.c` 通过共享 `gl_common` 输出：ProcessBuilder 合并 stdout/stderr，由 launcher collector 写入本次 `run.log`，并同步输出 logcat。DFRoot `bootstrap.c` 是 system-root 独立进程，logger 只写 logcat；它的 stdout 不属于 App/Shizuku `run.log`。既有 `/dev/dfm*` markers 仍是控制信号，日志不替代 marker 状态。
Shizuku `remove=true` 后，客户端等待 binder-disconnected 回调及其所在主线程队列轮次退出，再允许同 tag 的下一次 bind；该等待只串行化 UserService 销毁，不改变 launcher 选择规则。

### 11.8 Real-attack native build disconnection

2026-11-05 用户裁定先断开全部真攻击 backend。根 `app/src/main/cpp/CMakeLists.txt` 仍只做 project/标准设置与两个 `include()` 组装：`include(common/)`、`include(backends/)`；其中 `common/CMakeLists.txt` 声明 **OBJECT** target `gl_common`，`backends/CMakeLists.txt` 只接入非攻击的 Diagnose DSO，不接入 43499-Glued、43284-Glued、98260 的历史 chunk。这些 chunk 继续作为 source-only candidate 保留，不进入 AGP externalNativeBuild target graph，也不会以攻击 ELF 进入 APK。Gradle 同时删除对应 backend properties、M0 source staging/build task 与 marker wiring，因此没有 build property 旁路可以重新接回这些 chunk。

2026-11-06 用户进一步裁定删除 `verifyDebugNativeArtifacts`、`verifyReleaseNativeArtifacts` 和仅供其消费的 CMake target manifest。2026-11-08 用户又删除不必要的 `verifyJnaDistribution`；当前 root `nativeBuildAndroid` 只依赖 debug/release 双 variant 的 `assemble`。它证明两版 APK 可构建，不再声明 JNA distribution、merged ELF、ABI 或 JNI 精确核验。历史 verifier 的通过和失败只保留在当批证据中，不作为当前图的组件。

```mermaid
flowchart LR
  GRADLE[":app:externalNativeBuildDebug / assembleDebug"] --> ROOT["cpp/CMakeLists.txt：project/标准 + include()"]
  ROOT --> COMMON["common/CMakeLists.txt → OBJECT gl_common (.o)"]
  ROOT --> BACKENDS["backends/CMakeLists.txt：disconnection boundary"]
  BACKENDS --> DIAGNOSE["Diagnose DSO only"]
  BACKENDS --> NONE["no real-attack target"]
  SRC["43499glued / 43284glued / 98260 source-only"] -. not included .-> BACKENDS
  GRADLE --> DEBUG["assembleDebug"]
  GRADLE --> RELEASE["assembleRelease"]
  DEBUG --> ROOT_GATE
  RELEASE --> ROOT_GATE
  NONE --> APK["APK contains no project real-attack ELF"]
```

Host CTest 不复用上述 Android production CMake。backend/module 自己拥有最小 TU/stub mapping，root 只做组合；`native-test-disposition.tsv` 同时约束所有 test/probe 的处置，不能把 excluded row 当成测试 PASS。

```mermaid
flowchart LR
  SOURCE["app/src/main/cpp + tools<br/>suffix/prefix test + named probe"] --> INVENTORY["verifyNativeTestInventory<br/>36 candidates exact-set"]
  TSV["native-test-disposition.tsv<br/>machine authority"] --> INVENTORY
  TSV --> C43499["43499glued/tests/CMakeLists.txt<br/>registration drift guard"]
  TSV --> CCONTRACT["tools/tests/CMakeLists.txt<br/>registration drift guard"]
  INVENTORY --> T43499["nativeTest43499GluedHost<br/>configure → build → CTest 27"]
  C43499 --> T43499
  INVENTORY --> TCONTRACT["nativeTestContractsHost<br/>configure → build → CTest 1"]
  CCONTRACT --> TCONTRACT
  INVENTORY --> TAVB["avbcheck typed task<br/>configure → build → CTest 1"]
  T43499 --> HOST_GATE["root nativeTestHost<br/>29/29"]
  TCONTRACT --> HOST_GATE
  TAVB --> HOST_GATE
  HOST_GATE --> CHECK_ALL["root checkAll"]
  ANDROID_GATE["nativeBuildAndroid<br/>debug + release build"] --> CHECK_ALL
  XML["verifyDebugUnitTestXml<br/>152 / 0 / 0 / 0"] --> CHECK_ALL
```

Host CTest仍可把保留source作为测试输入，但它不是APK production graph或registry接线。这张图只表达source/build ownership；本轮没有运行Gradle、CTest、AVD或真机门，因此断开后的构建结果保持`NOT_RUN`，且不宣称任何漏洞ELF可运行。

### 11.9 2026-11-02：新进程 Flow 通道（历史；由 §11.12 typed RunHost 取代）

自 2026-11-02 起，攻击一律启动新进程；2026-11-03 的 Kotlin Clean Architecture 纵切又把 UI 与该执行通道隔开。Material/Miuix renderer 只消费 `RunUiState + RunActions`，`RunViewModel` 只调 `RunBackendUseCase`；唯一 outer adapter 才把 pure command/session 映射到既有 gate、dispatcher 与 launcher。物理 run 由 application-scoped `LogHub` 持有，Activity/VM observer 消失只 detach，不 cancel。symbol 仍只能是 `FlowEntryRegistry` 登记的 Flow 入口（`@FlowEntry` 函数，类必须实现 `Flow<*>`）。

```mermaid
flowchart LR
  UI[Material / Miuix Run renderer] --> SA[RunUiState + RunActions]
  SA --> VM[RunViewModel]
  VM --> UC[RunBackendUseCase]
  UC --> PORT[RunSessionPort]
  PORT --> ADAPTER[BackendRunSessionAdapter]
  ADAPTER --> SPEC[Captured BackendSpec + flowParams]
  ADAPTER --> GATE[BackendRunGate]
  ADAPTER --> COORD[LogHub application scope]
  COORD --> DISP[BackendRunDispatcher]
  DISP --> FLP[FlowLaunchParent.launchSymbol]
  FLP -->|APP executor| APL[AppProcessFlowLauncher]
  FLP -->|SHIZUKU executor| SPL[ShizukuProcessFlowLauncher]
  APL --> HOST[FlowHostService :flowhost app uid]
  SPL --> US[GhostlockUserService shell uid 2000]
  HOST --> REG[FlowEntryRegistry + FlowEntryValidator]
  US --> REG
  REG --> ENTRY[FlowEntry.entry FlowHostEnv]
  ENTRY --> FLOW[Flow.run]
  FLOW -->|43499-Glued| EXEC1[staging profile → exec lib43499-Glued.so]
  FLOW -->|43284Glued| IPSEC[AndroidIpSecRuntime.create context]
  IPSEC --> EXEC2[exec lib43284Glued.so + argv]
  EXEC1 --> EV[FlowHostProtocol verify evidence]
  EXEC2 --> EV
  EV --> MAP[FlowHostProtocol.mapTerminal]
  MAP --> ADAPTER
  ADAPTER --> SESSION[RunSessionState StateFlow]
  SESSION --> VM
  VM --> UI2[同一 state/actions → 两套外观 renderer]
```

关键契约：
- host 注入保留参数 `host.uid/pid/domain/run_dir/native_lib_dir/log_path`；调用方伪造 `host.*` 被拒；run_dir 由 host 创建（app filesDir / shell `/data/local/tmp`）并在终态 evidence 后 best-effort 清理。
- 43499 entry 为 context-free：params(profile.conf/bin/release) → staging → exec 上游 ELF（stdin + 三环境变量）；43284 entry 使用 host-local `FlowHostEnv.context`（B4b 例外）创建 IpSec runtime，正常退出 close；超时/取消 **不 close**（`ipsec=runtime-leaked` + `host_keepalive=true`，host 跳过自杀/清理，A11）。
- 每次 run 新进程（app pid / Shizuku 实例与 pid 变化）；one-shot；终态先回传再回收；无 fallback（执行体二选一）。
- 显式 abort 且 `abortable=true` 时，coordinator 只取消其持有的 launcher job，沿既有 App/Shizuku cancel/finally 顺序回收；rotation/VM clear 不触发这条路径；handoff 后的 late callback 不再写 session state。
- registry membership 由 app、`:flowhost`、Shizuku 三个 composition root 各捕获一次 immutable list；`FlowEntryRegistryAssembly` 只接显式 snapshot，不再自行读 `Backends.all`。三端用同一 canonical encoding 生成 64-bit content version + SHA-256；APP/Shizuku launcher 在 `prepare/prepareFlow` 前读取 host AIDL identity，任一 version/hash 不一致即具名拒绝。仅 debug 构建追加诊断 fixture（release 只允许生产入口）；该新 wire 分支已通过 AIDL/Kotlin compile，设备对拍仍未执行。

已交付证据（2026-11-02）：B2 App `:flowhost` 真机 smoke PASS；B4a 43499 / B4b 43284 生产 entry fixture 门禁 PASS（真实 IpSec create/close）；G9 registry 设备回归 PASS；B5c-1/B5c-2 删除旧 runner/双执行体/旧 ELF host 通道，全量单测 470/0/0，debug/androidTest/release 构建 PASS。**未验证**：真实漏洞 ELF 冷机门禁（待用户授权）、Shizuku flavor 真机（设备无 Shizuku）、release APK 运行、98260 legacy host 通道与 Lua 动态 symbol 注册（遗留 B5c-3/B5a/B5b）。

### 11.9.1 2026-11-02：后端注册表统一（BackendId 删除、单一注册表驱动 UI）

- 每个当前后端由一个 object 实现 Android outer contract `BackendSpec`：`key/selectable/flowSymbol/flowParams/readiness/runtimeSpec/rows/configSchema` 与 nullable `configurationSpec` 同在一个声明里；backend 声明不持有 Composable、renderer 或 frontend state。
- `Backends.all` 是唯一注册集合；AppContainer 启动期复制一次 membership，同一份 list 显式注入 catalog、selection、semantic configuration adapter 与 run adapter。`BackendSessionStore.selectedKey` 是 application-scoped 选择权威；原 process-global `BackendSelection` 已删除。`AndroidBackendConfigurationAdapter` 只能在 captured membership 中查找 `BackendSpec`，并委派标准 `configurationSpec.snapshot/mutate`；它不 import concrete backend，也不按 key/class 分支。
- configuration capability 只暴露 `Static/Toggle/ChoiceDocument/Document` 语义 DTO 与标准 mutation。presentation-owned `RegisteredBackendConfigRenderer` 只按语义 subtype 选择 generic renderer；皮肤只从 `LocalUiMode` 派生的 `LocalUiKit` 读取。R1 已删除 original 43499 的 A301SO/Mondrian config 与 request factory，不把其复制到 UI/domain/core/data。`isSelectable(spec)=spec.selectable`，无对象身份白名单。
- `BackendId` 枚举（旧 `Flow<*>` 绑定）与 `BackendSpec.id`/`customConfig` 已删除；`register` 运行时 fail-closed（空/重复 key、selectable 无 flowSymbol 拒绝）。debug probe 仅 DEBUG 注册，release 菜单不含。
- R1 current membership：`cve43499glued`、`cve43284glued`、`lua`，以及 debug 构建中的 `debugprobe`；original `cve43499`/`cve43284` 已连同手写 JNI 与专用测试从 current graph 删除。Lua 是唯一仍带 `@UnfinishedBackend(reason)` 的 builtin；debug probe 不标注。R1 的 software gate 结果记录在本计划对应批次，不复用旧设备结果冒充。

```mermaid
flowchart LR
  BUILTINS["43499-Glued / 43284-Glued / Lua<br/>+ DEBUG-only probe"] --> REG
  REG[Backends.all 唯一注册集合] --> ROOT[AppContainer 捕获 immutable membership]
  ROOT -->|domain projection| MENU[后端菜单 enabled=selectable]
  MENU -->|stable key| SELECT[BackendSelectionPort]
  SELECT --> SESSION[BackendSessionStore.selectedKey]
  ROOT --> CONFIG[AndroidBackendConfigurationAdapter]
  SPEC[BackendSpec.configurationSpec] --> CONFIG
  CONFIG --> STATE[BackendConfiguration semantic state]
  STATE --> CARD[RegisteredBackendConfigRenderer generic subtype dispatch]
  MODE[LocalUiMode] --> KIT[LocalUiKit]
  KIT --> CARD
  CARD --> UI[PathPage 配置卡]
  ROOT -->|spec.flowSymbol| FL[Run adapter → 新进程入口]
  ROOT -->|spec.readiness/flowParams| GATE[BackendRunGate/Dispatcher]
```

> 注：§11.7 的 exec-glue 类图（含 BackendId/旧 runner）为历史图；本图取代其“UI 选择/卡来源”部分。

### 11.10 2026-11-03：Kotlin Clean Architecture 与统一语义 renderer（D7 source 收口；设备终门待补）

```mermaid
classDiagram
  direction LR
  class Domain {
    UiPreferences / OperationalPreferences / AppSelection
    DeviceStatus / PrivilegeState / BackendDescriptor
    PayloadDefinition / FinishDefinition
    RunCommand / RunSessionState
    Ports + UseCases
  }
  class DataAdapters {
    five typed DataStore repositories
    Android device facts and picker effects
    Backend session-selection-run adapters
    Android privilege adapter
  }
  class TypedStores {
    ui-preferences.json
    operational-preferences.json
    app-selection.json
    payload.json
    finish.json
  }
  class AppContainer {
    main-process-only owner
    one lazy initialization barrier
    captured registry membership
    adapter/use-case ownership
    AppViewModelFactory
  }
  class ViewModels {
    immutable StateFlow
    semantic actions
  }
  class SemanticUI {
    Home / Backend / Settings / Payload / Finish / Run state+actions
  }
  class MaterialRenderer
  class MiuixRenderer
  class OuterExecution {
    BackendSpec / configurationSpec / registry identity
    gate / dispatcher / launcher / host
  }

  AppContainer --> DataAdapters : constructs once
  DataAdapters --> TypedStores : complete data class / actor-current transform
  DataAdapters ..|> Domain : implements ports
  AppContainer --> ViewModels : injects use cases
  ViewModels --> Domain : invokes
  ViewModels --> SemanticUI : publishes state/actions
  SemanticUI --> MaterialRenderer
  SemanticUI --> MiuixRenderer
  DataAdapters --> OuterExecution : maps only at outer boundary
```

#### 11.10.1 P-UI-ICON-001：项目图标单一入口

`com.ghostlock.app.ui.icon.Icons` 是 production source 唯一可直接 import Material Icons 与 Miuix Icons 的文件。每个当前语义/变体由一个不可变 `IconPair` 同时保存两侧 `ImageVector` 与独立 fallback 标志；共享 presentation 可用 `resolve(UiMode)` 选择皮肤，既有 caller 在本批显式取原先使用的一侧以保持页面行为不变。某一上游没有对应项时只把该侧映射到 Help，不能让另一侧一起退化。`kotlin_architecture_d0.py` 对目录外 upstream import 和 fully-qualified reference 都 fail-closed；P-UI-ICON-002 以后只扩充同一目录的生成条目，不新增第二个 resolver。

```mermaid
classDiagram
  direction LR
  class ProjectIcons["ui.icon.Icons"] {
    +Filled
    +Outlined
    +Rounded
    +AutoMirrored
    +Basic
    -pair(material, miuix) IconPair
  }
  class IconPair {
    +ImageVector material
    +ImageVector miuix
    +Boolean materialFallback
    +Boolean miuixFallback
    +resolve(UiMode) ImageVector
  }
  class UiMode {
    Material
    Miuix
  }
  class MaterialIcons["androidx Material Icons"]
  class MiuixIcons["Miuix Icons"]
  class PresentationCallers["Material / Miuix presentation callers"]
  class HelpFallback["per-side Help fallback"]

  MaterialIcons --> ProjectIcons : only direct import
  MiuixIcons --> ProjectIcons : only direct import
  ProjectIcons *-- IconPair : owns semantic pairs
  HelpFallback --> IconPair : fills only missing side
  IconPair --> UiMode : resolve
  PresentationCallers --> ProjectIcons : catalog entry
  PresentationCallers --> IconPair : explicit side or resolve
```

```mermaid
stateDiagram-v2
  [*] --> Idle
  Idle --> Running: explicit Start / gate allowed
  Running --> Aborting: explicit Abort and abortable
  Aborting --> Aborted: existing launcher cancellation cleanup returned
  Running --> Done: success / failure / refusal
  Running --> HandoffRequested: handoff terminal
  Done --> RerunReady: first primary action
  Aborted --> RerunReady: first primary action
  HandoffRequested --> RerunReady: first primary action
  RerunReady --> Running: confirm or warn disabled
```

边界与所有权：
- `domain/**` 不依赖 Android、Compose、backend/core/data/ui；Android resource、registry object、`FlowResult` 与 package manager 只在 adapter 外层映射。
- `AppSelection` typed DataStore 是 executor 与 backend stable key 的唯一持久 authority；`BackendSessionStore` 只保存经过当前 registry 校验的 process-local key。catalog、selection 与 run adapter 读取同一实例，不再经 process-global selection。只有需要系统内核偏移的 backend 使用 GLK profile，其它 backend 设置归自己的具名 typed authority；UI 不 import backend/data/core，data adapter 不 import concrete backend。
- A301SO/Mondrian debug config 与 43499 finish request factory 是 cve43499 attack-private owner；旧 `data/cve43499/A301SoDebugConfig.kt`、`core/finish/FinishHandoffAdapter.kt` 与零 caller 的 `core/finish/FinishExecution.kt` 已分别 move/move/delete，不再形成 backend→data 或 core→concrete-backend 倒置。
- Shizuku binder 与 permission listener 只归 `AndroidPrivilegeAdapter`；executor 选择写入 `AppSelection`，在线、permission、uid 与 SELinux domain 始终实时读取。Home dialog 与 Run gate/launcher 读取同一个 `PrivilegeState` snapshot，presentation 不直接调用 Shizuku 或平台存储。
- Settings 的外观控件只消费 `UiPreferences`，日志、运行提示与开机控件只消费 `OperationalPreferences + SettingsScreenActions`；两个完整 data class 分别由独立 typed DataStore 持有。字体字节保存在 app-private 文件，store 只保存固定相对引用；run adapter 使用同一 actor cutoff 返回的 operational 对象。
- `LogHub` 拥有 launcher coroutine；VM 只订阅 `StateFlow`。renderer 只读 state 并调用 actions，不能持有/close/cancel host、fd、payload 或 attack child。
- Payload 与 Finish 分别由 `DataStorePayloadRepository`、`DataStoreFinishRepository` 直接持有完整 data class；没有内部 version wrapper、JCS、legacy codec 或 profile 镜像。新安装 Finish 默认包含 `finish-restore-selinux`，显式空 Finish 仍用于 Payload-only preview 与 Diagnose。
- page-level `LocalUiMode` 分支继续决定 Material/Miuix 外观；Home/Backend/Run/Settings/About/ColorPalette/Payload/Finish 均先绑定同一语义 state/actions，再进入独立 renderer。Payload 使用 `PayloadPageMaterial` / `PayloadPageMiuix`，Finish 设置保持独立页面；共享语义层不抹平两套组件的几何、色彩或 typography。
- `kotlin_architecture_d0.py verify --section architecture` 对 domain 外向依赖、data/backend 反向 UI 依赖、非 UI Compose、UI 直连 data/backend/core、旧 persistence authority、secondary-process DataStore、UI 直连 Shizuku，以及 UI/data/core 的 concrete-backend edge fail-closed。
- R1 最终树软件门（2026-11-04）：`verifyDebugUnitTestXml` = 30 suites / 152 tests / 0 failures / 0 errors / 0 skipped；`nativeTestHost` = 29/29；`nativeBuildAndroid` = 3 件 AArch64 artifact / JNI 0；`compileDebugAndroidTestKotlin` 为 exit 0。三个保留 native tree 的 diff exit 0。R1 没有修改攻击算法，因而没有执行 AVD/真机攻击门；这些普通软件证据不宣称任何 backend 已在设备上取得产品定义的 root 成功。

<a id="run-view-r2"></a>

### 11.11 2026-11-08：typed DataStore、Payload / Finish 与 root-script 单一 authority（P-SETTINGS-IO-001/批次01）

```mermaid
flowchart LR
  PUI[Payload UI state/actions] --> PUC[Payload use cases]
  PUC --> PR[DataStorePayloadRepository]
  PR --> PD["payload.json\nPayloadDefinition"]

  FUI[Finish UI state/actions] --> FUC[Finish use cases]
  FUC --> FR[DataStoreFinishRepository]
  FR --> FD["finish.json\nFinishDefinition"]

  PUI -->|full-save transaction result| PREVIEW[Payload-only preview]
  PREVIEW -->|explicit empty Finish| RENDER[RootScriptRenderer]
  PD -->|actor cutoff handle| FREEZE[RunInputFreezer]
  FD -->|actor cutoff handle| FREEZE
  OP["operational-preferences.json\nOperationalPreferences"] -->|actor cutoff handle| FREEZE
  FREEZE --> RENDER[RootScriptRenderer]
  RENDER --> TEMP[exclusive temp writer]
  TEMP --> CLOSE[flush + fsync + close + chmod]
  CLOSE --> MOVE["same-directory atomic move\nAndroid app-data rename; no hard-link"]
  MOVE --> PUB["RootScriptArtifact\nscript object+bytes identity\npre-created result object identity"]
  PUB --> CARRIER[backend-private rooted carrier]
  CARRIER --> SCRIPT["one script: Payload fail-fast → Finish best-effort"]
  SCRIPT --> GLR1["GLR1 fixed-order result\ncomplete / durable handoff"]
  GLR1 --> CODEC[RootScriptResultCodec]
```

```mermaid
sequenceDiagram
  participant UI as Payload/Finish UI
  participant PRepo as Payload typed repository
  participant FRepo as Finish typed repository
  participant Store as DataStore actors
  participant Preview as PreviewRootScriptUseCase
  participant Run as RunInputFreezer
  participant Renderer as RootScriptRenderer
  participant Workspace as isolated preview workspace

  UI->>PRepo: submit actor-current Payload transform
  PRepo->>Store: updateData
  Store-->>UI: exact committed PayloadDefinition
  UI->>Preview: exact full-save result
  Preview->>Renderer: Payload + explicit empty Finish
  Renderer->>Workspace: create result object; write/fsync/close/publish script
  Workspace-->>UI: preview script path
  UI->>PRepo: queue Payload cutoff
  UI->>FRepo: queue Finish cutoff
  PRepo->>Store: identity updateData at actor position
  FRepo->>Store: identity updateData at actor position
  Store-->>Run: exact immutable cutoff objects
  Note over Preview,Run: later edits remain behind each cutoff and apply to the next preview or run
```

P-RUN-IO-001/批次01 owner 与行为边界：

- `domain/payload` 只拥有主目标、顺序、`required` 与 acceptance；`domain/finish` 只拥有附加动作、`severity` 与 `alwaysRun`。二者都不包含 backend root 原语或 carrier。
- `RootScriptRenderer` 是唯一整脚本 writer；`PayloadScriptRenderer` 与 `PayloadVerifier` 只产生 Payload action/acceptance 片段。没有 `PayloadExecutor`、`FinishExecutor` 或 JVM root session。
- script temp 完成 write/flush/fsync/close/chmod 后才在0700 fresh workspace内以同目录atomic move发布，move保留同一inode；不使用Android `untrusted_app` SELinux禁止的hard-link。`PublishedArtifactIdentity` 从同一 `O_NOFOLLOW` opened fd 取得 device/inode/mode/size/SHA-256。result object 先以 exclusive ordinary file 创建，解析时在同一 opened fd 上重验 object identity并读取bytes。Payload预览动作以`Result<String>`跨ViewModel/UI边界，Material/Miuix只投影成功路径或可见失败Toast，发布异常不得终止主线程。
- `RootScriptResultCodec` 只接受 GLR1 的 ASCII、LF-only、固定顺序、完整 item 四元组与 `complete=1`；unknown/duplicate/missing/out-of-order/partial、非法 state-reason-exit conjunction、Payload fail-fast 违规、Finish alwaysRun 违规或 reboot handoff 违规均整体拒绝，不从日志补猜。
- preview 通过 `PreviewRootScriptPublisher` 调同一个 renderer，但每次使用新的 `root-script-preview/<runId>` workspace；P-RUN-IO-001/批次01 只生成 artifact，不选择 backend、不执行脚本、不把 raw uid 0 投影为产品成功。
- 原R2实施批按用户裁定未做扩展验证；2026-11-05的R2-H1纠错只补了Android 15/内核6.6 AVD上的真实Payload Save发布路径：同目录atomic move成功、Toast显示published path、App进程与`MainActivity`保持存活、crash buffer为空、workspace mode/result/script/无temp均符合合同。完整fault-injection、GLR1语义与替换攻击矩阵仍未执行；后续只补缺失矩阵，不重复R1的SHA/基线检查。

<a id="run-view-r3"></a>

### 11.12 2026-11-05：P-RUN-X-001 neutral backend 与 typed RunHost（current authority）

本节取代 §11.7、§11.9 与 §11.9.1 的 current execution/membership 图。旧节只保留历史沿革；current tree 不再含动态注册、Flow 反射、registry digest、Bundle host 参数、旧 AIDL 或 Glued Kotlin connector。P-RUN-X-001/批次01 建立 execution spine；批次02 只将 RunHost 纯合同归 `core/runhost`、Android client/wire/host adapter 归 `data/runhost`、通用 FD connector 归 `data/log`，并把 App framework entry 改为 `runhost/AppRunHostService`。AIDL、state machine、Flow/LogHub、Payload/Finish 与成功语义不变。selectable PRODUCT_ROOT backend 数量仍为 0；debug/release都包含`DIAGNOSTIC_ONLY`的`DIAGNOSE`，CUSTOM仍disabled。P-DIAG-NATIVE-001/批次01新增的JNA/shared-library细节及原子prepare提交以§11.16为current authority。

#### 11.12.1 Class 与 owner

~~~mermaid
classDiagram
  direction LR

  class BackendDescriptor {
    +String key
    +BackendSelectionPolicy selectionPolicy
    +AbortPolicy abortPolicy
    +ExecutionDomain executionDomain
    +RunCapability runCapability
  }

  class BackendRuntime {
    <<interface>>
    +availability()
    +createFlow() Flow
  }

  class BackendRuntimeEntry {
    +BackendDescriptor descriptor
    -BackendRuntime runtime
    +availability()
    +createFlow() Flow
  }

  class BackendUi {
    <<interface>>
    +categoryLabelRes Int
    +displayNameRes Int
    +IconPair icon
    +Content(availability)
  }

  class BackendUiEntry {
    +BackendDescriptor descriptor
    +BackendUi ui
  }

  class BackendUiCatalog {
    +List~BackendUiEntry~ entries
    +selectedEntry()
    +availabilityFor(key)
    +displayName(entry)
  }

  class BackendDefinition {
    -BackendDescriptor descriptor
    -BackendRuntime runtime
    -BackendUi ui
    +runtimeEntry()
    +uiEntry()
  }

  class BackendRegistry {
    -List~BackendDefinition~ definitions
    +List~BackendDescriptor~ descriptors
    +List~BackendUiEntry~ uiEntries
    +descriptor(key)
    +runtime(key) BackendRuntimeEntry
    +availability(key)
  }

  class Flow {
    <<interface>>
    +run(BackendFlowInput) RunReport
    +stop()
  }

  class RunInputFreezer {
    +freeze(BackendDescriptor) FrozenRunInput
    +release(FrozenRunInput)
  }

  class AndroidRunSessionAdapter {
    +refresh()
    +start(RunCommand)
    +abort()
    +shareTerminal()
  }

  class LogHub {
    +StateFlow~RunSessionState~ state
    +start(context, execute)
    +requestAbort()
  }

  class IRunHost {
    <<AIDL>>
    +setCallback(runId, generation, callback)
    +prepare(RunHostRequest)
    +start(runId, generation)
    +getSnapshot(runId, generation)
    +cancel(runId, generation)
    +requestClose(runId, generation)
    +destroy()
  }

  class AppRunHostClient
  class ShizukuRunHostClient
  class AppRunHostService
  class GhostlockUserService
  class AndroidRunWorkspace
  class RootArtifactStagingAdapter
  class RunHostRequestWireCodec
  class RunHostSnapshotWireCodec
  class ProductRunPreparer
  class RunReport {
    +RunCapability runCapability
    +DiagnosticOutcome diagnosticOutcome
    +StageResult backendPrimitive
    +StageResult rootScriptDispatch
    +PayloadItemResult[] payloadItems
    +StageResult payloadOverall
    +FinishItemResult[] finishItems
    +StageResult finishOverall
    +StageResult backendCleanup
    +RunTerminal terminal
    +Boolean isProductSuccess
  }

  BackendRegistry o-- BackendDefinition : sole immutable membership
  BackendDefinition *-- BackendDescriptor
  BackendDefinition *-- BackendRuntime
  BackendDefinition *-- BackendUi
  BackendRegistry --> BackendRuntimeEntry : backend-only projection
  BackendRegistry --> BackendUiEntry : UI-only projection
  BackendRuntimeEntry *-- BackendDescriptor
  BackendRuntimeEntry --> BackendRuntime
  BackendUiEntry *-- BackendDescriptor
  BackendUiEntry --> BackendUi
  BackendUiCatalog o-- BackendUiEntry : process-local presentation projection
  BackendRuntime --> Flow : fresh one-shot instance
  AndroidRunSessionAdapter --> BackendDescriptor : captured exact stable key
  AndroidRunSessionAdapter --> RunInputFreezer : freeze once
  AndroidRunSessionAdapter --> LogHub : application-lifetime owner
  AndroidRunSessionAdapter --> AppRunHostClient
  AndroidRunSessionAdapter --> ShizukuRunHostClient
  AppRunHostClient --> IRunHost
  ShizukuRunHostClient --> IRunHost
  AppRunHostService ..|> IRunHost : app flowhost process
  GhostlockUserService ..|> IRunHost : Shizuku shell process
  AppRunHostService --> AndroidRunWorkspace
  GhostlockUserService --> AndroidRunWorkspace
  AppRunHostService --> RootArtifactStagingAdapter
  GhostlockUserService --> RootArtifactStagingAdapter
  AppRunHostService --> RunHostRequestWireCodec
  AppRunHostService --> RunHostSnapshotWireCodec
  GhostlockUserService --> RunHostRequestWireCodec
  GhostlockUserService --> RunHostSnapshotWireCodec
  AppRunHostService --> ProductRunPreparer
  GhostlockUserService --> ProductRunPreparer
  AppRunHostService --> BackendRegistry : runtime(key) only
  GhostlockUserService --> BackendRegistry : runtime(key) only
  IRunHost --> RunReport : typed immutable snapshot
~~~

Owner 边界固定如下：`composition/backend`的BackendRegistry私有拥有唯一immutable definition list；每个BackendDefinition只在outer composition同时引用纯descriptor、backend runtime与Android UI。AppContainer取得descriptor/UI projection并构造唯一BackendUiCatalog；普通inner caller不得import outer definition/registry。`core/runhost`只拥有platform-neutral protocol/model/state machine；`data/runhost`拥有client、AIDL codec、Android host adapter；`runhost/AppRunHostService`与`shizuku/GhostlockUserService`是两个framework entry。仅这两个entry可以调用`BackendRegistry.runtime(key)`并立即只持有BackendRuntimeEntry，不能读取UI、definition或membership list。application data adapter拥有frozen source与request PFD；RunHost在prepare时dup descriptor，并拥有staged input、workspace、fresh Flow、callback与terminal snapshot；App host workspace位于app-private no-backup root，Shizuku workspace位于shell-owned `/data/local/tmp/<package>-runhost`，两者都核验non-symlink与actual owner；LogHub拥有client coroutine；UI/ViewModel只订阅neutral state。BackendDescriptor、RunHostRequest、AIDL snapshot与profile都不携带BackendDefinition、BackendRuntime、BackendUi或Flow object。旧caller-zero process-carrier helper不属current tree；未来真实carrier必须随具名消费者重建。

#### 11.12.2 prepare、run 与 result Sequence

~~~mermaid
sequenceDiagram
  autonumber
  participant UI as Run UI
  participant VM as RunViewModel
  participant AD as AndroidRunSessionAdapter
  participant RG as BackendRegistry
  participant FR as RunInputFreezer
  participant RC as LogHub
  participant CL as App or Shizuku RunHostClient
  participant RH as typed RunHost
  participant ST as RootArtifactStagingAdapter
  participant FL as fresh Flow
  participant RS as common root script

  UI->>VM: start selected stable key
  VM->>AD: RunCommand(key)
  AD->>RG: captured descriptor key and dynamic availability callback
  RG-->>AD: neutral BackendDescriptor and availability
  AD->>FR: freeze descriptor, profile v3, Payload, Finish
  FR-->>AD: FrozenRunInput(runId, generation, artifacts)
  AD->>RC: start application-owned run
  RC->>CL: typed RunHostRequest plus read-only PFDs
  CL->>RH: setCallback(runId, generation)
  CL->>RH: prepare(request)
  RH->>RH: Parcel budget and structural validation
  RH->>RH: state, identity and semantic fingerprint preflight
  RH->>RG: runtime(key) exact projection
  RG-->>RH: BackendRuntimeEntry with descriptor and runtime
  RH->>RH: capability shape, actual UID plus SELinux domain, readiness validation
  RH->>ST: dup and stage every PFD into host workspace
  ST-->>RH: verified host-owned artifact identities
  RH->>FL: create one fresh Flow; constructor loads backend DSO when native-backed
  RH->>RH: build complete PreparedRun plus PREPARED snapshot candidate
  RH->>RH: host-lock atomic lifecycle PREPARED plus candidate publication
  RH-->>CL: prepare OK
  CL->>RH: start(runId, generation)
  RH->>FL: run(BackendFlowInput)
  FL-->>RH: BackendRunFacts primitive, dispatch, cleanup, diagnostic
  opt PRODUCT_ROOT future carrier
    FL->>RS: dispatch exact published artifact
    RS-->>RH: typed GLR1 Payload and Finish item facts
  end
  RH->>RH: compose immutable six-layer terminal snapshot
  par low-latency callback
    RH-->>CL: onTerminal(snapshot)
  and durable authority polling
    CL->>RH: getSnapshot(runId, generation)
    RH-->>CL: typed current or terminal snapshot
  end
  CL-->>RC: RunReport
  RC-->>VM: RunSessionState(report)
  VM-->>UI: six layers plus independent diagnostic and product projector
  CL->>RH: requestClose(runId, generation)
  RH->>RH: terminal retention decision and workspace cleanup
  RH-->>CL: DESTROYED accepted
  CL->>RH: App unbind or Shizuku remove
~~~

request 不包含 RunCapability。successful prepare 后的 capability 只能由命中的BackendRuntimeEntry所携带的BackendDescriptor投影。callback event 只作 bounded progress；terminal snapshot 才是 result authority。client 不依赖 terminal callback 必达，而是以 getSnapshot 读取持久 authority。late callback 必须匹配 current runId、generation、strictly-increasing sequence 且尚未 terminal，否则 client 丢弃且不得改变 report。

#### 11.12.3 RunHost state

~~~mermaid
stateDiagram-v2
  [*] --> NEW
  NEW --> NEW: setCallback reserves identity
  NEW --> NEW: Flow constructor/load failure; delete workspace and return IO_FAILURE
  NEW --> PREPARED: complete candidate plus snapshot atomically published under host lock
  PREPARED --> PREPARED: same identity and fingerprint retry / ALREADY_APPLIED
  PREPARED --> RUNNING: start
  RUNNING --> RUNNING: duplicate start / ALREADY_APPLIED
  RUNNING --> TERMINAL: Flow terminal snapshot published
  NEW --> TERMINAL: safe cancel after identity reservation
  PREPARED --> TERMINAL: cancel before physical start
  RUNNING --> RUNNING: safe cancel reserves cancellation; resources stay live
  RUNNING --> TERMINAL: worker stops access, completes cleanup, publishes cancelled terminal
  RUNNING --> RUNNING: unsafe cancel or requestClose rejected
  NEW --> DESTROYED: requestClose after identity reservation
  PREPARED --> DESTROYED: requestClose
  TERMINAL --> DESTROYED: requestClose after retention decision
  DESTROYED --> DESTROYED: repeated requestClose / ALREADY_APPLIED
  DESTROYED --> [*]: framework destroy or service unbind
~~~

generation 小于 current identity 返回 stale，generation 大于 current 返回 busy，同 generation 不同 runId 返回 mismatch。framework destroy 只在 requestClose 已把状态推进到 DESTROYED 后生效；RUNNING 的直接 destroy 不清理 worker 或攻击资源。

#### 11.12.4 产品结果投影

~~~mermaid
flowchart TD
  S[typed terminal snapshot] --> C{RunCapability}
  C -->|DIAGNOSTIC_ONLY| D[DiagnosticOutcome and DIAGNOSTIC_COMPLETE or UNKNOWN]
  D --> N[isProductSuccess = false]
  C -->|PRODUCT_ROOT| P{RunTerminal = SUCCESS}
  P -->|yes| Y[isProductSuccess = true]
  P -->|no| F[isProductSuccess = false]
  L[stdout, logcat, callback event, raw uid 0, process exit 0] -. diagnostic only .-> N
  L -. cannot upgrade .-> F
~~~

UI 同时显示 capability、diagnostic、backend primitive、root-script dispatch、Payload、Finish、backend cleanup 与 terminal。`DIAGNOSE`的PASS加DIAGNOSTIC_COMPLETE只证明typed transport/JNA/shared-library/log/shell fixture完成；它不代表root产品成功。首个真实backend进入registry前，必须由自己的carrier子计划提供root-script identity、六层facts与目标设备门。

### 11.13 2026-11-05：R4/R4A 历史纠正记录（runtime topology 已由 §11.14 取代）

本节只保留沿革，不是current topology、current fixture或运行门权威。R4 不改变 §11.12 的 RunHost state 或六层结果语义；caller-zero SessionPhase 已删除，而旧 LogExport 与automatic-export preference、tree picker、ViewModel action、双皮肤controls、strings曾被误删。2026-11-05用户纠正后，R4A曾恢复产品能力但没有恢复旧object；随后R4B推翻R4A runtime topology。当前唯一结构权威见§11.14，运行门仍为`NOT_RUN`。

~~~mermaid
flowchart LR
  S[Settings UI] --> L[log level]
  S --> F[terminal font]
  S --> T[timestamp]
  U[explicit diagnostic action] --> B[AndroidDiagnosticReportBuilder]
  B --> R[LogHub private-file GLLOG2 prefix]
  X[automatic export preference and picker] --> P[OperationalPreferences]
  P --> C[application LogHub]
  C --> D[data/run MediaStore worker]
  G[architecture source guard] --> A[old AIDL absent]
  G --> J[manual JNI absent outside named frozen candidates]
  G --> C[UI domain core concrete-backend edges absent]
  G --> P[backend product-script generator absent]
~~~

上图只说明R4误删与R4A纠正的历史范围，不描述当前sink实现。旧`log/LogExport.kt=DELETE/ABSENT/R4`事实继续保留；R4B的current source、fixture与`NOT_RUN`边界全部收敛到§11.14，避免两节同时声称current authority。

<a id="run-view-r4c"></a>

### 11.14 2026-11-08：P-LOG-X-001/批次03 LogConnector + 双流 LogHub（current topology）

`LogHub`是唯一应用日志中枢，也是全局`commitId`的唯一owner。它只依赖`domain/run.LogConnector`与`LogDraft`，不知道PFD、AIDL、JNA、shell grammar或具体backend。Kotlin/coroutine producer使用`KotlinLogConnector`；Native/Shell pipe使用`FdLogConnector`；App/Shizuku oneway callback使用`RunHostLogConnector`。每个connector保留自己的FIFO；RunHost sequence只检测该transport的gap/duplicate/inversion/final delivery，source clock只作元数据。跨source顺序固定为draft被LogHub raw ingress接受的先后，不按timestamp或transport sequence回溯重排。

每次accepted run拥有独立connector scope，并允许执行期间动态attach/detach。正常结束顺序固定为producer停止并join/reap→关闭write side或发送terminal marker→connector drain accepted prefix→返回`Drained/Incomplete`→关闭reader/transport→LogHub追加普通Warn（仅Incomplete）→普通Info RunReport摘要→End；abort才允许提前close/cancel。`Incomplete`不改变typed report或产品成功。Android data adapter仍按冻结请求预构造未打开的private-file和可选MediaStore sink；LogHub接受后才补UI sink。raw、processed、Kotlin/RunHost source及每个sink queue均为`Channel.UNLIMITED`，不设累计记录/字节阈值；单条wire/Parcel边界继续有效，所有无界队列仍受真实进程资源约束。

~~~mermaid
flowchart LR
  K[Kotlin and Flow producers] --> KC[KotlinLogConnector]
  N[Native pipe source] --> FC[FdLogConnector]
  S[Shell handoff source] --> FC
  FC -->|validated LogDraft FIFO| KC
  KC --> RH[RunHost callback forwarder]
  RH --> RC[RunHostLogConnector]
  LC[future/direct local connector] -. same LogConnector contract .-> H[LogHub dynamic attach]
  RC --> H
  H --> R[raw unlimited FIFO: Start Record Clear Barrier End]
  T[Flow typed RunReport] -->|nine ordinary Info drafts after source drain| R
  R --> P[single processor: threshold commitId hub elapsed GLLOG2 row]
  P --> Q[processed unlimited FIFO]
  Q --> D[single nonblocking dispatcher]
  D --> U[UI sink unlimited queue]
  D --> F[private-file sink unlimited queue]
  D --> M[optional MediaStore sink unlimited queue]
  U --> V[append-only rows and O1 prefix snapshot]
  V --> W[ViewModel pass-through]
  W --> X[Material and Miuix keyed LazyColumn]
  F --> Y[buffered private log]
  Y --> Z[manual share and diagnostic exact flushed prefix]
  M --> A[buffered MediaStore output]
  T --> B[typed terminal and product-success authority]
  B -. not reconstructed from text .-> X
  ST[source timestamp and RunHost sequence] -. metadata and integrity only; never sort .-> R
~~~

~~~mermaid
classDiagram
  class LogConnector {
    <<interface>>
    +collect(emit LogDraft) LogConnectorEnd
    +close()
  }
  class LogConnectorEnd {
    <<sealed>>
    Drained
    Incomplete(reason)
  }
  class KotlinLogConnector {
    -Channel~LogDraft~ input
    +emit(LogDraft)
    +finishInput()
    +finishInputIncomplete(reason)
  }
  class FdLogConnector {
    -InputStream readEnd
    +collect(emit) LogConnectorEnd
  }
  class RunHostLogConnector {
    -Channel~Event~ events
    -RunHostLogCursor cursor
    +receive(RunHostLogRecord)
    +receiveTerminal(snapshot)
    +finishIncomplete(reason)
  }
  class LogHub {
    -Channel raw
    -Channel processed
    +StateFlow state
    +start(context, request, storageSinks, execute attach)
    +filePrefix()
    +clearTerminal()
  }
  class LogDocumentFormatter {
    +formatHeader(identity)
    +formatRecord(record, config)
    +formatTrailer(phase)
  }
  class LogSinkBinding {
    +LogSinkRole role
    +LogSink sink
  }
  class LogRowsSnapshot {
    +Long token
    +Long revision
    +Int size
    +get(index) LogRecord
  }
  class AndroidLogSinkFactory {
    +create(request, onFailure) List~LogSinkBinding~
    -PrivateFileSink
    -MediaStoreSink
  }
  class AndroidRunSessionAdapter {
    +start(command)
  }
  LogConnector <|.. KotlinLogConnector
  LogConnector <|.. FdLogConnector
  LogConnector <|.. RunHostLogConnector
  LogConnector --> LogConnectorEnd
  LogHub --> LogConnector : run-scoped dynamic attach and drain
  LogHub --> LogDocumentFormatter : sole row encoder
  LogHub --> LogSinkBinding : one run-owned list
  LogHub --> LogRowsSnapshot : publishes UI prefix
  AndroidRunSessionAdapter --> AndroidLogSinkFactory : create unopened storage sinks
  AndroidRunSessionAdapter --> RunHostLogConnector : one per App or Shizuku request
  AndroidRunSessionAdapter --> LogHub : pass storage bindings to start
  AndroidLogSinkFactory --> LogSinkBinding : private and optional MediaStore
  LogSinkBinding --> LogSink
~~~

~~~mermaid
sequenceDiagram
  participant UI as Run UI
  participant A as AndroidRunSessionAdapter
  participant C as AndroidLogSinkFactory
  participant H as LogHub
  participant RC as RunHostLogConnector
  participant CL as App or Shizuku client
  participant RH as RunHost
  participant KC as KotlinLogConnector
  participant FC as Native/Shell FdLogConnector
  participant P as raw processor
  participant D as processed dispatcher
  participant F as private-file worker
  participant M as optional MediaStore worker
  participant V as UI sink
  UI->>A: start selected backend
  A->>C: create frozen request's unopened storage sinks
  C-->>A: private-file and optional MediaStore bindings
  A->>H: start frozen context, request, storage bindings
  alt run rejected
    H->>F: abort unopened sink without file I/O
    H->>M: abort unopened sink without MediaStore I/O
  else run accepted
    H->>H: reserve token; create UI sink; append it to same binding list
    H->>P: raw Start then initial Info
    A->>RC: create for exact run identity
    A->>H: attach RC in run-scoped connector scope
    A->>CL: run(request, RC)
    CL->>RH: setCallback + prepare + start
    RH->>KC: create source before Flow
    RH->>RH: start KC collector before producer
    opt current Diagnose native chain
      RH->>FC: create Native and Shell readers before JNA call
      FC-->>KC: complete validated drafts in physical FIFO
      RH->>RH: native returns; close write ends
      FC-->>RH: EOF then Drained or accepted-prefix Incomplete
      RH->>FC: close read endpoints after collect
    end
    loop each accepted RunHost source record
      KC-->>RH: source draft FIFO
      RH-->>CL: onLog with source-private sequence
      CL->>RC: receive(record), validate identity/sequence/line
      RC-->>H: LogDraft through attached collect
      H->>P: raw Record in acceptance order
      P->>P: threshold, commitId, elapsed, encode once
      P->>D: processed Entry
      D-->>V: role-list tryEmit same event
      D-->>F: role-list tryEmit same event
      D-->>M: role-list tryEmit same event if enabled
    end
    RH->>KC: Flow stopped; finishInput
    KC-->>RH: drain then Drained/Incomplete
    RH->>KC: close after collect
    RH-->>CL: immutable terminal snapshot plus callback marker
    CL->>RC: receiveTerminal(final sequence, dropped count)
    CL->>CL: one completion deadline waits snapshot + marker + connector end
    alt contiguous marker and no drops
      RC-->>H: Drained after accepted FIFO
    else gap, duplicate, peer death, timeout, or source failure
      RC-->>H: Incomplete after accepted prefix
      H->>P: ordinary Warn draft; typed result unchanged
    end
    H->>RC: close transport after collect
    CL-->>A: typed RunReport outcome
    H->>H: freeze new attach; await all connector jobs
    H->>P: nine ordinary Info rows under same threshold, then End
    P->>D: ordered processed rows and End
    D-->>V: nonblocking enqueue
    D-->>F: nonblocking enqueue
    D-->>M: nonblocking enqueue
    H-->>UI: typed phase/report without waiting for slow sink
    opt active manual share or diagnostic
      A->>H: filePrefix()
      H->>P: raw FileBarrier
      P->>D: processed FileBarrier
      D-->>F: enqueue barrier
      F->>F: flush preceding rows once
      F-->>A: stable path and exact byte length
      A->>A: copy only that prefix
    end
  end
~~~

Connector lifecycle：

~~~mermaid
stateDiagram-v2
  [*] --> Attached: LogHub accepts connector in active run
  Attached --> Collecting: hub-owned collector starts
  Collecting --> WaitingForEnd: source FIFO temporarily empty
  WaitingForEnd --> Collecting: producer accepts another draft
  Collecting --> Draining: producer stopped and write side closed or marker queued
  WaitingForEnd --> Draining: EOF or marker becomes observable
  Draining --> Drained: complete FIFO plus valid terminal evidence
  Draining --> Incomplete: gap, malformed tail, peer death, read error, or missing marker
  Collecting --> Incomplete: run abort closes reader then cancels collector
  Drained --> Closed: LogHub closes final reader or transport resource
  Incomplete --> Closed: accepted prefix retained; ordinary Warn only
  Closed --> [*]
~~~

Sink worker lifecycle：

~~~mermaid
stateDiagram-v2
  [*] --> Created: accepted run creates handles
  Created --> Opening: worker starts
  Opening --> Writing: private file or MediaStore output opened
  Opening --> Failed: open failure
  Writing --> Writing: buffered Entry
  Writing --> Writing: FileBarrier flush and prefix completion
  Writing --> Closing: End writes session/end and flushes
  Writing --> Failed: write or flush failure
  Closing --> Closed: close success
  Closing --> Failed: close failure
  Failed --> Closed: fail pending barriers and release owned stream
  Closed --> [*]
~~~

`RunReport`仍归Flow并作为typed terminal与产品成功权威；其九条展示摘要只是Kotlin/Info raw records，Warn或Error阈值会在UI、private-file、MediaStore和manual/diagnostic日志中共同过滤它们。LogHub只在全部已attach connector结束后提交这些摘要与End；connector失败仅产生一条普通Warn。UI sink维护完整本次run的append-only数组，发布固定prefix的`LogRowsSnapshot`；ViewModel不对历史全量`map`，两个renderer只按可见index读取，更不会按`sourceElapsedMs`排序。`UiClear`只替换UI backing list，不删除private-file或MediaStore内容。private-file始终启用，活动期barrier完成精确已flush前缀，terminal后由发起分享/诊断的协程等待file worker结束；processor、dispatcher、Flow terminal不等待任何sink I/O。`GLLOG2`只有header、meta、entry、session、end物理行，没有隐藏typed report trailer。source不完整和sink故障都不改变typed结果。

<a id="run-view-r5"></a>

### 11.15 2026-11-05：R5-S1 common product host closure（common spine retained；generic carrier declaration retired）

本节描述仍保留的common host结构。2026-11-08的P-BACKEND-CORE-001/批次01删除了无真实PRODUCT_ROOT consumer的`BackendCarrierArtifact`与`PackagedBackendArtifactResolver`，`RunInputFreezer`不再自动注入一份声明式carrier；这不是删除未来carrier能力。43499、43284或98260若重新获批，必须在自己的纵向设计中把实际carrier作为标准frozen input artifact交给同一staging/publish链，并冻结其source owner与lifecycle。当前没有concrete product runtime进入registry，canonical profile-v3 encoder/decoder仍是独立enable blocker，因此本节不把PRODUCT_ROOT通路标为可达、Ready、selectable或supported。

#### 11.15.1 IPO与单一权威

~~~mermaid
flowchart LR
  P[canonical profile fixture or future v3 source] --> O[OpenedRunInputSource<br/>one O_NOFOLLOW object]
  M[PayloadRootManagers + PackageManager + uname.release] --> MR[RootManagerArtifactResolver<br/>mandatory executable + actual argv]
  MR --> O
  FC[future concrete backend carrier<br/>standard frozen artifact only] -. not connected .-> O
  O --> L[FrozenRunInputLease<br/>opaque token to master PFD]
  L -->|duplicate only| W[typed RunHost request PFDs]
  W --> S[RootArtifactStagingAdapter<br/>EXECUTABLE 0500 / other 0400]
  S --> A[Map logicalId to PublishedFlowArtifact]
  R[validated RunHostRequestModel] --> N[RootScriptExecutionPlan<br/>kind policy staged path exact argv acceptance]
  A --> N
  N --> E[RootScriptRenderer]
  N --> D[RootScriptResultCodec]
  N --> X[RunReportComposer]
  E --> F[exact RootScriptArtifact + pre-created GLR1 object]
  F --> B[BackendFlowInput<br/>report context + terminal claim + log emitter]
  B --> G[Flow computes BackendRunFacts]
  G --> X
  D --> X
  X --> T[Flow returns six-layer RunReport]
  T --> U[RunHost snapshots and transports report]
  Z[stdout log callback raw uid0] -. diagnostic only; cannot upgrade .-> T
~~~

#### 11.15.2 Class/owner关系

~~~mermaid
classDiagram
  class FrozenRunInputLease {
    +FrozenRunInput input
    +duplicate(token) ParcelFileDescriptor
    +close()
  }
  class PublishedFlowArtifact {
    +String logicalId
    +RunInputArtifactKind kind
    +String path
    +PublishedArtifactIdentity identity
  }
  class RootScriptExecutionPlan {
    +String runId
    +Long generation
    +List payload
    +List finish
    +validate()
  }
  class ProductRunPreparer {
    +prepare(workspace, identity, request, staged)
  }
  class RootScriptRenderer
  class RootScriptResultCodec
  class RunReportComposer
  class BackendFlowInput {
    +Map artifacts
    +String? persistentStateDirectory
    +FlowReportContext reportContext
    +FlowTerminalClaim terminalClaim
    +LogEmitter logs
    +complete(BackendRunFacts) RunReport
  }
  FrozenRunInputLease *-- FrozenRunInput
  ProductRunPreparer --> PublishedFlowArtifact
  ProductRunPreparer --> RootScriptExecutionPlan
  ProductRunPreparer --> RootScriptRenderer
  ProductRunPreparer --> BackendFlowInput
  BackendFlowInput --> RootScriptResultCodec
  BackendFlowInput --> RunReportComposer
~~~

`FrozenRunInputLease`独占masters；request assembly只取得duplicates。全部duplicate成功或失败后masters立即close，随后只删除app-private profile source；root-manager ELF及未来具体backend提供的carrier source由各自source owner持有，不由lease删除。RunHost独占staged paths/workspace；Flow只借用catalog与stable backend-state path。

#### 11.15.3 App/Shizuku共享sequence

~~~mermaid
sequenceDiagram
  participant A as AndroidRunSessionAdapter
  participant L as FrozenRunInputLease
  participant H as App or Shizuku RunHost
  participant S as RootArtifactStagingAdapter
  participant P as ProductRunPreparer
  participant F as fresh Flow
  participant C as RootScriptResultCodec
  participant R as RunReportComposer

  A->>L: freeze profile + carrier + manager opened objects
  A->>L: duplicate each opaque token into request
  A->>L: close masters and owned private source
  A->>H: prepare typed request
  H->>H: structure/capability/registry/domain/readiness gates
  H->>S: stage owned PFDs and close them
  S-->>H: staged identity list + published catalog
  H->>P: prepare(validated model, staged list)
  P->>P: build and validate one RootScriptExecutionPlan
  P->>P: publish exact script + pre-created result object
  P-->>H: PreparedProductRun
  H->>F: run(BackendFlowInput + catalog + stable state dir)
  F->>F: compute BackendRunFacts and emit LogDraft
  F->>H: FlowTerminalClaim.claim() through neutral port
  H-->>F: cancelledBeforeClaim or ownership lost
  alt scriptDispatch is SUCCESS
    F->>C: decode same result object with same plan
    C-->>F: complete GLR1 or malformed
  else dispatch not SUCCESS
    F->>F: choose NotAttempted without reading result object
  end
  F->>R: same plan + backend facts + decode outcome + cancellation
  R-->>F: six-layer RunReport
  F-->>H: RunReport
  H-->>A: immutable terminal snapshot
~~~

App 与 Shizuku 各自保留现有 lifecycle、domain gate 和 callback owner；两者都先用同一个 `ProductRunPreparer` 发布 plan/script，再由 fresh Flow 自己解码 result、取得中立 terminal claim 并组合 `RunReport`。控制方向始终是 RunHost service 调 Flow；Flow 不调用 `AppRunHostService` 或 `GhostlockUserService`。APP-only backend 在 Shizuku domain gate 先被拒绝；host 不按 backend key 硬编码分支。

#### 11.15.4 Lease、workspace与retention状态

~~~mermaid
stateDiagram-v2
  [*] --> OPENED: O_NOFOLLOW open + fstat + same-fd digest + rewind
  OPENED --> DUPLICATING: request assembly
  DUPLICATING --> CLOSED: all duplicates created; close masters
  DUPLICATING --> CLOSED: any failure; close partial duplicates and masters
  CLOSED --> OWNED_SOURCE_REMOVED: app-private source only
  CLOSED --> PACKAGED_PRESERVED: packaged carrier or manager
  OWNED_SOURCE_REMOVED --> [*]
  PACKAGED_PRESERVED --> [*]

  state RunWorkspace {
    [*] --> PREPARED
    PREPARED --> TERMINAL_SAFE: known terminal without held state
    PREPARED --> TERMINAL_RETAINED: UNKNOWN or NEEDS_ATTENTION or REBOOT_REQUIRED or REBOOT_REQUESTED
    TERMINAL_SAFE --> DELETED: requestClose
    TERMINAL_RETAINED --> PRESERVED: requestClose releases host owner without deleting evidence
  }
~~~

R5-S1 test source已覆盖same-opened inode、owned-source终结、packaged/root-manager解析、lease-to-wire mandatory executable/actual argv、mixed normalized kinds、同plan render/decode、dispatch未开始合成与six-layer compose。遵用户指令，这些测试及Gradle、instrumentation、AVD、Shizuku和设备门均未运行，状态只记`source complete / NOT_RUN`。

<a id="run-view-r6-diagnose"></a>

### 11.16 2026-11-06：P-DIAG-NATIVE-001/批次01 Diagnose JNA/shared-library纵向（current authority）

本节只取代§11.12中Diagnose membership、native-backed Flow构造和prepare提交细节；§11.12继续拥有通用RunHost/结果模型，§11.14继续拥有唯一`LogHub`与双流sink拓扑，§11.15继续拥有PRODUCT_ROOT公共资产链。`DIAGNOSE`在debug/release均可见，但固定为`DIAGNOSTIC_ONLY`：不取得root、不读取Payload/Finish结果、不恢复任何真实攻击backend，`isProductSuccess`恒为false。

#### 11.16.1 IPO与唯一日志路径

~~~mermaid
flowchart LR
  I[Input<br/>DIAGNOSE key + profile artifact + App/Shizuku domain] --> P[Prepare<br/>stage artifact + construct Flow/binding + Native.load]
  P --> C[Atomic commit<br/>complete candidate + PREPARED snapshot]
  C --> HB[Accepted start<br/>bind workspace root as GHOSTLOCK_HOME]
  HB --> F[DiagnoseFlow : SequentialFlow<br/>one diagnose-native-chain step]
  F --> N[JNA direct mapping<br/>one ordinary C call]
  N --> J[libjnidispatch.so]
  J --> D[libghostlock_diagnose.so<br/>gl_diagnose_run]
  D --> NP[Native pipe<br/>one exact Native record]
  D --> RH[common run_handoff<br/>fixed location + exact marker + same-opened fd]
  RH --> S[posix_spawn<br/>/system/bin/sh /proc/self/fd/198]
  S --> SP[Shell pipe<br/>one exact Shell record]
  RH --> D
  F --> K[Kotlin milestone record]
  NP --> E[LogEmitter]
  SP --> E
  K --> E
  E --> HC[RunHost sequence/callback]
  HC --> L[LogHub<br/>raw/processed dual stream and fan-out]
  F --> O[Output<br/>primitive + dispatch facts<br/>DIAGNOSTIC_ONLY RunReport]
  O --> HC
  L --> U[UI / manual share / automatic export / diagnostics]
~~~

输入authority只有immutable `BackendDescriptor`、typed RunHost request、host-owned workspace和host-published diagnostic script；JNA binding不接收Context、library path、环境变量或profile loader设置。Kotlin mapping与project DSO随同一App发布，request/result只含实际业务字段，不设version、struct size、reserved或旧兼容分支，也不做运行时layout/header探测。输出authority只有C函数的signed 32-bit call status、一次提交的result、primitive/dispatch两项`StageResult`、唯一cleanup fact及Flow组合的`RunReport`。Native/Shell文本只作为日志source进入`LogHub`，不得升级任何typed fact。

#### 11.16.2 Class与生命周期owner

~~~mermaid
classDiagram
  class DiagnoseBackendDefinition {
    +BackendDescriptor descriptor
    +DiagnoseBackendRuntime runtime
    +DiagnoseBackendUi ui
  }
  class DiagnoseBackendRuntime {
    +availability()
    +createFlow() Flow
  }
  class Flow {
    <<interface>>
    +run(BackendFlowInput) RunReport
    +stop()
  }
  class SequentialFlow {
    +steps(input)
    +cleanup(input)
    +diagnosticOutcome(runs, cleanupFact)
  }
  class DiagnoseFlow {
    -DiagnoseNativeBinding binding
    +steps(input)
    +cleanup(input)
  }
  class DiagnoseNativeBinding {
    -DiagnoseJnaLibrary library
    -AtomicReference activeCall
    +runChain(logs, nonce) ChainResult
    +close()
  }
  class DiagnoseJnaLibrary {
    <<JNA Library>>
    +gl_diagnose_run(request, result) Int
  }
  class GhostlockDiagnoseDSO {
    <<shared library>>
    +gl_diagnose_run(request, result) int32_t
  }
  class CommonHandoff {
    <<common native>>
    +run_handoff() int32_t
  }
  class LogHub {
    +start(context, execute)
    +raw and processed channels
    +sink fan-out
  }

  DiagnoseBackendDefinition --> DiagnoseBackendRuntime : backend-only projection
  DiagnoseBackendRuntime --> DiagnoseFlow : fresh factory
  DiagnoseFlow --|> SequentialFlow
  SequentialFlow ..|> Flow
  DiagnoseFlow *-- DiagnoseNativeBinding : one-shot owner
  DiagnoseNativeBinding --> DiagnoseJnaLibrary : immutable mapping
  DiagnoseJnaLibrary --> GhostlockDiagnoseDSO : ordinary C symbol via JNA dispatch
  GhostlockDiagnoseDSO --> CommonHandoff : same synchronous native operation
  DiagnoseFlow --> LogHub : only through RunHost LogEmitter/callback
~~~

`DiagnoseFlow`只拥有binding并把一个physical step的结果映射为两项typed fact；它不拥有shell process、stream或script path。`DiagnoseNativeBinding`拥有immutable mapping与每次call的两组pipe；DSO在Native与Shell两个不重叠的窗口分别借用write fd并立即复制，返回前detach自己的duplicate。common helper只借用当前Shell窗口的export fd，把同一fd复制为child stdout/stderr，并同步wait/reap自己创建的child。binding `close()`不伪装DSO unload；`LogHub`只拥有应用侧日志双流与sink，不拥有Flow、native handle或shell process。

#### 11.16.3 prepare、native、shell与report Sequence

~~~mermaid
sequenceDiagram
  autonumber
  participant CL as App/Shizuku client
  participant RH as AppRunHostService/GhostlockUserService
  participant RG as BackendRegistry
  participant FL as DiagnoseFlow
  participant BD as DiagnoseNativeBinding
  participant JNA as JNA/libjnidispatch
  participant DSO as libghostlock_diagnose.so
  participant CH as common run_handoff
  participant SH as /system/bin/sh
  participant LH as LogHub

  CL->>RH: prepare(DIAGNOSE request)
  RH->>RH: validate, create workspace, stage profile, publish diagnostic script
  RH->>RG: runtime(DIAGNOSE)
  RG-->>RH: BackendRuntimeEntry
  RH->>FL: runtimeEntry.createFlow() constructs DiagnoseFlow
  FL->>BD: DiagnoseNativeBinding constructor
  BD->>JNA: Native.load("ghostlock_diagnose", mapping class)
  JNA-->>BD: immutable mapping or throw
  alt load/factory throws
    RH->>RH: delete workspace; lifecycle remains NEW
    RH-->>CL: IO_FAILURE; no RunReport/log
  else complete candidate
    RH->>RH: build PreparedRun + PREPARED snapshot candidate
    critical host lock
      RH->>RH: lifecycle.prepare(identity, fingerprint)
      RH->>RH: publish prepared + snapshot together
    end
    RH-->>CL: OK
  end
  CL->>RH: start(identity)
  RH->>RH: Os.setenv(GHOSTLOCK_HOME, workspace.root)
  RH->>FL: run(BackendFlowInput)
  FL->>RH: Kotlin diagnose:start via BackendFlowInput.logs
  RH->>LH: callback record accepted
  FL->>BD: runChain(logs, nonce)
  BD->>BD: create Native/Shell pipes; initialize direct request/result fields
  BD->>JNA: gl_diagnose_run(request, result)
  JNA->>DSO: ordinary C call
  DSO->>DSO: validate pointers + HANDOFF_NOT_RUN; capture uid/pid/nonce
  DSO->>DSO: attach Native fd; write one exact record; detach
  DSO->>DSO: attach Shell fd
  DSO->>CH: run_handoff()
  CH->>CH: read GHOSTLOCK_HOME; openat fixed path O_NOFOLLOW
  CH->>CH: validate regular file + exact marker on same fd
  CH->>SH: posix_spawn fixed shell with script fd + Shell log fd
  SH-->>CH: exact Shell record; exit status
  CH->>CH: synchronous wait/reap
  CH-->>DSO: handoff status
  DSO->>DSO: detach Shell fd; commit result once
  DSO-->>BD: call status + uid/pid/nonce/handoff status
  BD->>BD: close both write ends; bounded independent drains; strict decode
  BD->>RH: Native record via BackendFlowInput.logs
  RH->>LH: callback record accepted
  BD->>RH: Shell record via BackendFlowInput.logs
  RH->>LH: callback record accepted
  BD-->>FL: one ChainResult
  FL->>FL: map primitive + dispatch facts independently
  FL->>BD: close pipe resources only
  FL-->>RH: DIAGNOSTIC_ONLY RunReport
  RH-->>LH: typed terminal report via client
~~~

两个execution domain必须分别证明constant-name load和round-trip；App `:flowhost`成功不能替代Shizuku UserService。任何域失败都保持该域NOT PASS，不新增PFD staging、自由path、JNI fallback或第二loader。

#### 11.16.4 prepare与调用状态

~~~mermaid
stateDiagram-v2
  [*] --> NEW
  NEW --> LOAD_MAPPING: validated staging complete; construct Flow
  LOAD_MAPPING --> NEW: factory/load throwable; delete workspace; IO_FAILURE
  LOAD_MAPPING --> CANDIDATE_READY: mapping loaded; complete PreparedRun and snapshot candidate
  CANDIDATE_READY --> PREPARED: host-lock atomic lifecycle plus publication
  CANDIDATE_READY --> PREPARED: concurrent identical winner already published / loser deletes own workspace
  PREPARED --> RUNNING: accepted start obtains non-null prepared
  RUNNING --> HOME_BOUND: set current workspace GHOSTLOCK_HOME
  HOME_BOUND --> NATIVE_CHAIN: one SequentialFlow physical step
  HOME_BOUND --> CLEANUP: setenv failure claims terminal
  NATIVE_CHAIN --> HANDOFF_ACTIVE: Native record emitted; common helper entered
  NATIVE_CHAIN --> DRAIN: call-status failure before helper
  HANDOFF_ACTIVE --> RESULT_COMMITTED: shell child reaped; result committed once
  RESULT_COMMITTED --> DRAIN: JNA returned; close both write endpoints
  DRAIN --> CLEANUP: independent records decoded; two facts mapped
  CLEANUP --> TERMINAL: same cleanup fact feeds diagnostic outcome and RunReport
  TERMINAL --> [*]
~~~

`LOAD_MAPPING`、`CANDIDATE_READY`、`HOME_BOUND`与`NATIVE_CHAIN`等是prepare/Flow内部阶段，不扩展公开`RunHostState`枚举。公开状态仍只有NEW/PREPARED/RUNNING/TERMINAL/DESTROYED；constructor failure必须留在NEW，成功PREPARED提交必须同时使`prepared`与snapshot可见。DIAGNOSE使用`BEFORE_START_ONLY`，进入同步native chain后不引入跨线程native取消状态；common helper必须先wait/reap自己的shell child，再允许result提交和JNA返回。

### 11.17 2026-11-08：当前 data 层边界与旧 43499 搬入代码清理

本节描述 current production tree 的 `data/` 边界。§11.7 中的 HOCON、generic profile resolver、wire/route 与 Lua repository 只保留为 exec-glue 历史图，不再代表当前实现；本次没有改变内置 kernel profile 资产、extractor、冻结的 native backend source 或任何 Flow/RunHost 行为。

~~~mermaid
flowchart TB
  APP[AppContainer / Android composition root]

  subgraph DATA[current data adapters and repositories]
    SETTINGS[data/settings<br/>five typed DataStores]
    BACKEND[data/backend<br/>selection + session + run adapters]
    RUN[data/run<br/>workspace + staging + sink]
    DEVICE[data/device + privilege + diagnostics + update]
  end

  subgraph MIGRATION[isolated legacy profile migration seam]
    LEGACY[LegacyProfileConverter]
    VALUE[ValueModel]
    LEGACY --> VALUE
  end

  REMOVED[removed dead copied cluster<br/>HOCON loader + generic profile/resolver/exporter<br/>wire/route models + component placeholder<br/>Lua document repository]

  APP --> SETTINGS
  APP --> BACKEND
  APP --> RUN
  APP --> DEVICE
  REMOVED -. no current production edge .- DATA
  MIGRATION -. no current runtime caller .- APP
~~~

`LegacyProfileConverter` 是 AGENTS §4 指定的唯一 v1/v2→v3 迁移点，因此本批保持源码与 package 不变；`ValueModel` 是它的直接模型依赖，同样保留。其余删除项在删除前没有 UI、Flow、RunHost、repository、Gradle task 或测试 caller，`com.typesafe:config` 也只服务已删除的 `HoconSupport`，因此依赖同批移除。若未来恢复某个需要系统内核偏移的真实 backend，应按该 backend 的 GLK profile 权威重新设计实际 consumer，而不是恢复这套无 owner 的 generic pipeline。
