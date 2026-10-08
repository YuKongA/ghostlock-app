# 全流程 UML（**权威**）· 新架构（A0–A6）

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

## 3. Class（新树 10 层 + 资源面 + 暴露面映射）

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
  class Pipeline { 组合固定 · 编译期 }
  class Backend { 步骤 + route（prepare→execute→disarm→destroy） }
  class Terminal { 接管 · 声明式收尾 }
  class Memory { waiter/PI 资源 · RAII }
  class Support { 中性工具 }
  class Profile { 数据面取值 }
  class RuntimeTime { 基址/KASLR 换算 }
  class Rust { extractor：boot/OTA → conf/json }
  Kotlin --> Wire
  Wire --> NativeEntry
  NativeEntry --> ScriptSandbox
  ScriptSandbox --> SurfaceTable
  SurfaceTable --> Contract
  SurfaceTable --> Backend
  SurfaceTable --> Profile
  NativeEntry --> Pipeline
  Pipeline --> Backend
  Pipeline --> Terminal
  Backend --> Memory
  Backend --> RuntimeTime
  NativeEntry --> Support
  Rust --> Kotlin
```

### 3.0 新树模块锚点（A0–A6）

| 新树模块 | 层 | 在本件的位置 |
|---|---|---|
| **`AttackWindow`** | 窗口宿主（窗口相位） | §1 窗口相位 · §2.3 窗口状态机 · §4.1 |
| **`OpRegistry`** | 操作面（暴露面条目的实现登记，与 surface 表同源） | §3 SurfaceTable→实现层 · §3.1 |
| **`script_surface`** | 契约层暴露面（**7 条 + `cfg`**） | §3 SurfaceTable · §3.1 |
| **`plan_gate`** | 校验（校验对象 = staged） | §2.1 · §4.1 · §4.3 |
| **`Document` 三段** | Kotlin resolve 产物 | §1 · §4.4 |

### 3.1 暴露面 7 条 → 实现层

| 条目 | 实现层 | 相位 | 备注 |
|---|---|---|---|
| `backend(id, {route?})` | 组合根/目录 | 窗口外 | **构造 = 无副作用的选择** |
| `close(h)` | 组合根/目录 | 窗口外 | generation 递增；旧句柄 ⇒ `handle-stale` |
| `b:writeop{target={kind,value}, value, width}` | backend route/step | 窗口相位 | **kind = 机制枚举**，value 来自 `cfg` |
| `b:copyop{src,dst={kind,value}, width}` | backend route/step | 窗口相位 | 结构复制（宽度 = profile 声明尺寸） |
| `b:settle{us}` | **`race`**（代码权威：`contract/script_surface.hpp:206` 的 `implements_layer`；层级断言见 `:305`） | 窗口相位 | 参数化（不得 C++ 兜底） |
| `b:handoff{terminal, payload_ref}` | terminal/handoff | 窗口外→terminal | **引用语义**（命令内容属 ③ 段） |
| `cfg`（只读快照） | profile（快照注入） | stage 前 | 只读；不属句柄面 |

### 3.2 拒绝码（gate + 边界校验）

| 码 | 触发 |
|---|---|
| `surface-unknown-name` | 表外名字（默认拒绝） |
| `handle-conflict` | 同 backend 两个活跃句柄 |
| `handle-stale` | 使用已释放/陈旧句柄 |
| `param-invalid` / `param-missing` | 参数越界 / 缺失（不兜底） |
| `width-mismatch` | `copyop` 宽度 ≠ profile 声明尺寸 |
| `geometry-missing` | 数据面缺几何/偏移 |
| `run-form-invalid`（**已随 `run` 移除而作废**） | — |
| `executor-unavailable` / `backend-not-available` | 可用性门禁 |

### 3.3 Kotlin / Rust 位置

| 件 | 位置 | 职责 |
|---|---|---|
| Kotlin | `app/` + `profile-core/` | HOCON 解析/合并/覆盖 · `resolve`（Document 三段）· payload 生成 |
| Rust | `tools/extract_rs/` | boot.img / OTA → profile（`--format conf` / `json`） |

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

## 5. 维护约定

- **一处权威**：本文件是唯一权威 UML；其他文档**链接**本件，不重画同一结构。结构变化（新增/删除层、暴露面条目、拒绝码、相位）⇒ **同批**更新本件对应图。
- **引用**：`docs/analysis/adr/README.md` · `handoff-payload-batch-plan.md` · `contract-design.md` · `plugin-runtime-integration-design.md` · `root-manager-selection-design.md` · `docs/archive/*` · `engineering-rules.md` · `AGENTS.md`。
- **两条新纪律（同批留痕）**：本件任何**新条目/新相位/新实现层**必须同时满足 —— ① **无名字→值表**（名字只允许 **dispatch 标识符** 与 **数据面键路径**；见 [target 附件](../analysis/lua-surface-annex-target.md) §C1）· ② **表达一致**（同一件事只允许一种表达，不得「首设 A 语法 / 后续 B 语法」；见 [lua-surface-design](../analysis/lua-surface-design.md) 开头）。
- **历史**：旧实现 UML = [old-full-process-uml.md](old-full-process-uml.md)（归档头含理由与恢复来源；登记见 `docs/archive/INDEX-plans.md` §七）。
