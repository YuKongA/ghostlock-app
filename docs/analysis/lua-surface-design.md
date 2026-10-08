# Lua 暴露面设计（绑定面 = **实际暴露什么就绑什么**）

> **表达一致**（用户 2026-10-08）：同一件事**只允许一种表达**，不得「首设用 A 语法、后续用 B 语法」分裂（例：route **只在 acquire 给**，换路 = `close` + 重新 acquire）。
>
> 分析类（≤8 KiB）。**自包含**。**口径（用户 2026-10-08 修正）**：绑定面**不是**「只能 `contract`」，而是「**我们实际暴露什么就绑什么**」⇒ 先有**暴露面表**（唯一权威），再按表绑定；**表外名字 ⇒ 具名拒绝**。
> 相关：[ADR-0008](../analysis/adr/0008-lua-runtime-sandbox.md) · [ADR-0010](../analysis/adr/0010-lua-cpp-resource-model.md)（**待批**）· [P5 规格](../analysis/handoff-script-layer-p5-spec.md) · [rewrite-plan-v2](../plan/rewrite-plan-v2.md)。

## §1 暴露面表（**唯一权威**；表外名字一律拒绝）

| 条目名 | 形态 | 参数界 | 语义 | 实现层 | stage 期 | 执行期 | 相位 | 持资源 | 拒绝码 |
|---|---|---|---|---|---|---|---|---|---|
| `backend(id, {route?})` | 函数 `(id, opts) → 句柄` | `id ∈` 目录 token 集 · **`route`（可选，仅在 acquire 给）** | 取 backend 句柄；**route 只在 acquire 时给定**，无「切换」动作 | 组合根/目录 | 允许 | 允许 | 否 | 否 | `surface-unknown-token` · `backend-not-available` |
| **`close(h)`** | 函数（释放） | `h` = 活跃句柄 | 释放（换路 = 先 `close` 再 `backend(...)`）；**generation 递增**（旧句柄 ⇒ `handle-stale`） | 组合根/目录 | 允许 | 允许 | 窗口外 | 否 | `handle-stale` · `handle-conflict` |
| `b:writeop{target, value, width}` | 句柄方法 | `target ∈ profile 符号名` · `value` 整数 · `width` 必填 | **字写**（覆盖旧 Zero/任意字写）；窗口内原子写 | backend route / step | 允许（stage 记录） | 允许 | **窗口相位**（运行时内部分组） | 否（句柄只标识） | `param-invalid` · `geometry-missing` · `op-not-available` |
| **`copyop{src,dst,width}`** | 句柄方法 | profile 符号名 · `width` = profile 声明结构尺寸 | **结构复制**（覆盖旧 Credential） | backend route / step | 允许 | 允许 | 窗口相位 | 否 | `param-invalid` · `width-mismatch` |
| `b:settle{us}` | 句柄方法 | `us` ≤ profile 上界 | 时间等待（参数化，不得 C++ 兜底） | `support/timing` | 允许 | 允许 | 窗口相位 | 否 | `param-invalid` · `param-missing` |
| `b:handoff{terminal, payload_ref}` | 句柄方法 | `terminal ∈` 目录 token 集 · **`payload_ref` = Kotlin resolve 生成的交付物引用** | **交接（引用语义）**：只声明 terminal 种类 + payload 引用；**命令内容不进脚本**（见 §4.2） | terminal / handoff | 允许 | 允许 | **否** | 否 | `surface-unknown-token` · `executor-unavailable` · `payload-missing` |
| ~~`run`~~（**已移除**：属 ③ 段，见 §1.2） | — | — | — | — | — | — | — | — | — |
| **`cfg`**（只读快照） | 表（快照） | 字段 ∈ manifest 声明集合 | **脚本可读的只读快照**（取代 `profile.get`） | profile 层（快照注入） | 允许 | 只读允许 | 否 | 否 | `cfg-unknown-field` |
| 语言级循环 / 条件（`for`/`while`/`if`） | **语言结构**（非条目） | 迭代次数受**预算**约束 | 控制流（录制可展开、重放须一致） | Lua 沙箱 | 允许 | 允许 | **否**（窗口内零 Lua） | 否 | `budget-exceeded` |
| **表外任意名字** | — | — | — | — | **拒绝** | **拒绝** | — | — | **`surface-unknown-name`** |

- **列语义**：实现层 = 条目落到的层；stage/执行 = 单遍两段是否放行；相位 = 运行时内部；持资源 = 一律否。
- **窗口级 op 已降为内部**：脚本不表达窗口分组（`writeop`/`copyop`/`settle` 属之）。
- `profile.get` 已删（`cfg` 取代）。

## §1.1 什么**不进**表

- **驱动器参数**（attempts/settle_us/verify/退避）⇒ profile（30 访问器见 [rewrite-plan-v2](../plan/rewrite-plan-v2.md) §4.2）；**route 几何** ⇒ 数据面。
- **机制常量留 C++**（`WriteMode` · waiter `0x30` · 页 `4096` · futex 码 · 偏移），须注释；**运行期量不可配置**（目标地址 · 喷溅命中页 · 泄漏 task · waiter 地址 · pid）。

## §1.2 Document（Kotlin resolve 产物）= 三段 + 零隐性回退

- **三段**：① **数据面**（HOCON 合并后）· ② **计划**（**Lua 文本 + 大小 + sha256**）· ③ **收尾 payload**（脚本/LKM，**bin + sha256**，Kotlin 生成）。
- **执行**：root 前 **stage 一次** ⇒ gate **校验 staged** ⇒ **按相位执行** ⇒ 按 ③ 段收尾；**零隐性回退**。
- **三条推论**（详版见 [Document 附件](lua-surface-annex-document.md)）：① 脚本控制流**属 document 行为**，禁 **runtime 自加回退**（显式允许/隐性禁止）· ② **换路须写进 document**（**runtime 永不自己换路**）· ③ **`run` 随 ③ 段移除** ⇒ 暴露面 **七条**（含 `close`），**`TD-RW2-2` 作废**。

## §2 绑定权威（**单一名单**）

- **持有者**：`surface` 表（`constexpr`）= 唯一权威；宿主按表注册（集合 ≠ 表 ⇒ 启动期失败）。**机器判据**：① 注册集合 == 表；② 条目名只定义一次；③ 沙箱符号级证据。
- **判据形式**：表外调用 ⇒ **具名拒绝** `surface-unknown-name`（**默认拒绝**，不做近似匹配）。
- **一致性**：表与绑定同源；「先看绑定再补表」= 违例。

## §3 单遍执行（**跑脚本 → stage → gate 校验 → 按相位执行**）

1. **跑脚本**：窗口外一次 ⇒ **staged 计划**。
2. **gate 校验（对象 = staged）**：十条 + surface 表；不重跑脚本；失败 ⇒ 具名拒绝。**3. 执行**：按 staged；脚本不参与。

- **逐调用边界校验（保留）**：每次调用校验参数界/句柄/相位；越界 ⇒ 具名拒绝 + 立即终止。
- **判据（三条，可证伪）**：
  - **stage 零副作用**：stage 期无写系统调用/资源获取（证据 = 符号级 + 观测）；
  - **确定性**：两次 stage 逐字节相同；
  - **staged == 执行**：逐条逐值一致；偏差 ⇒ 终止 + 记缺陷。

## §3.1 纪律放宽（**TD-RW2-1**）

- 「**用户脚本正确性 = 用户责任**」⇒ 不做脚本逻辑静态证明；**保留**：窗口内零 Lua · 逐调用校验 · gate 校验 staged · 预算；**代价**：改坏可能 panic；**偿还 = 首个 G2 后复评**（登记见 [待批单](../plan/A6-baseline-approval.md) §5）。

## §4 资源与窗口

- **句柄边界**：脚本只持表内句柄；POD `{kind,index,generation}` · 有界 64 · 无 `__gc`（[ADR-0010](../analysis/adr/0010-lua-cpp-resource-model.md) **待批**）；不得自造/延长/跨域。
- **窗口内**：**脚本本身在窗口内不执行**（窗口内零 Lua）；窗口相位动作由实现层按 **staged 计划**执行（`writeop`/`settle`）；其余条目**不得**出现在窗口相位。
- **资源归属**：窗口资源由窗口宿主独占（RAII 逐路径回收）；缺参数 ⇒ 具名拒绝。

## §4.1 三层收尾（声明式）

- **收尾三声明**：`terminal{on_success, always, on_failure}`（**声明式**），取代过程式拼装。
- **内核兜底留 C++**：relay/enforce 恢复与 watchdog 留 C++。
- **被取代（沿革不删）**：旧 `root_cmd` 模板被取代（语义仍作输入）。

## §4.2 handoff 归属（**Kotlin 负责**）

- HOCON 只声明意图；Kotlin `resolve` 产 **payload（脚本 + LKM + 大小 + sha256）** ⇒ wire 传入 ⇒ native 校验 + 落盘 ⇒ terminal 执行（三段 + 内核兜底 C++）。

## §6 待裁

| # | 待裁定 | 选项 | 保守默认 |
|---|---|---|---|
| **Q1** | `settle` 进窗口 | ① 允许 ② 仅窗口外 | **①** |
| **Q2** | `width` 缺省 | ① 即拒 ② 取 profile | **①** |
| **Q3** | 脚本承载 | ① 资产 ② 内联 | 待定 |
| **Q4** | 中途换路的真实用例 | 有 / 无 | 待裁 |
| **Q5** | 自动回退 | ❌ 与禁回退裁定冲突（需强制越权） | 待裁 |
