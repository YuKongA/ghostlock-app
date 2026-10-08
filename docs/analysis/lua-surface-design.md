# Lua 暴露面设计（绑定面 = **实际暴露什么就绑什么**）

> **表达一致**（用户 2026-10-08）：同一件事**只允许一种表达**，不得「首设用 A 语法、后续用 B 语法」分裂（例：route **只在 acquire 给**，换路 = `close` + 重新 acquire）。
>
> **禁止 C++ 名字→值表**（名字只允许 ① dispatch 标识符 ② 数据面键路径）⇒ [target 附件](lua-surface-annex-target.md)。
>
> 分析类（≤8 KiB）。**自包含**。**口径（用户 2026-10-08 修正）**：绑定面**不是**「只能 `contract`」，而是「**我们实际暴露什么就绑什么**」⇒ 先有**暴露面表**（唯一权威），再按表绑定；**表外名字 ⇒ 具名拒绝**。
> 相关：[ADR-0008](../analysis/adr/0008-lua-runtime-sandbox.md) · [ADR-0010](../analysis/adr/0010-lua-cpp-resource-model.md)（**待批**）· [P5 规格](../analysis/handoff-script-layer-p5-spec.md) · [rewrite-plan-v2](../plan/rewrite-plan-v2.md)。

## §1 暴露面表（**唯一权威**；表外名字一律拒绝）

| 条目名 | 形态 | 参数界 | 语义 | 实现层 | stage 期 | 执行期 | 相位 | 持资源 | 拒绝码 |
|---|---|---|---|---|---|---|---|---|---|
| `backend(id, {route?})` | 函数 `(id, opts) → 句柄` | `id ∈` 目录 token 集 · **`route`（可选，仅在 acquire 给）** | **构造 = 无副作用的选择**（选实现 + 选 route）；route 只在 acquire 给，无「切换」动作（三层划分见 [target 附件](lua-surface-annex-target.md) §C5） | **`backend`**（构造经组合根/目录校验） | 允许 | 允许 | 否 | 否 | `surface-unknown-token` · `backend-not-available` |
| **`close(h)`** | 函数（释放） | `h` = 活跃句柄 | 释放（换路 = 先 `close` 再 `backend(...)`）；**generation 递增**（旧句柄 ⇒ `handle-stale`） | **`session`**（句柄表；构造经组合根/目录校验） | 允许 | 允许 | 窗口外 | 否 | `handle-stale` · `handle-conflict` |
| `b:writeop{target, value, width}` | 句柄方法 | **`target = {kind, value}`**（kind = 机制枚举 · value **来自 `cfg`**）· `width` 必填 | **字写**（覆盖旧 Zero/任意字写）；窗口内原子写 | **`backend`**（route/step 为其内部） | 允许（stage 记录） | 允许 | **窗口相位**（运行时内部分组） | 否（句柄只标识） | `param-invalid` · `geometry-missing` · `op-not-available` |
| **`copyop{src,dst,width}`** | 句柄方法 | **`src`/`dst` = `{kind, value}`** · `width` = profile 声明结构尺寸 | **结构复制**（覆盖旧 Credential） | **`backend`**（route/step 为其内部） | 允许 | 允许 | 窗口相位 | 否 | `param-invalid` · `width-mismatch` |
| `b:settle{us}` | 句柄方法 | `us` ≤ profile 上界 | 时间等待（参数化，不得 C++ 兜底） | `race` | 允许 | 允许 | 窗口相位 | 否 | `param-invalid` · `param-missing` |
| `b:handoff{terminal, payload_ref}` | 句柄方法 | `terminal ∈` 目录 token 集 · **`payload_ref` = Kotlin resolve 生成的交付物引用** | **交接（引用语义）**：只声明 terminal 种类 + payload 引用；**命令内容不进脚本**（见 §4.2） | **`terminal`** | 允许 | 允许 | **否** | 否 | `surface-unknown-token` · `executor-unavailable` · `payload-missing` |
| ~~`run`~~（**已移除**：属 ③ 段，见 §1.2） | — | — | — | — | — | — | — | — | — |
| **`cfg`**（只读快照） | 表（快照） | 字段 ∈ manifest 声明集合 | **脚本可读的只读快照**（取代 `profile.get`） | **`profile`**（快照注入） | 允许 | 只读允许 | 否 | 否 | `cfg-unknown-field` |
| 语言级循环 / 条件（`for`/`while`/`if`） | **语言结构**（非条目） | 迭代次数受**预算**约束 | 控制流（录制可展开、重放须一致） | **`script`**（Lua 沙箱） | 允许 | 允许 | **否**（窗口内零 Lua） | 否 | `budget-exceeded` |
| **表外任意名字** | — | — | — | — | **拒绝** | **拒绝** | — | — | **`surface-unknown-name`** |

- **口径（实现层）**：**实现层 = `contract::kLayers` 的层名**（10 个；见 [`script_surface.hpp`](../../src/core/contract/script_surface.hpp) 的 `implements_layer` 与层级断言）；**机器可校验**，**不得用描述性短语** —— 「**表达一致**」的一个实例。
- **列语义**：stage/执行 = 单遍两段是否放行；相位 = 内部；持资源 = 否（**实现层口径见下**）。
- **窗口级 op 已降为内部**：脚本不表达窗口分组。

- **不进表的量**（驱动器参数 · route 几何 · 机制常量 · 运行期量）⇒ 见 [target 附件](lua-surface-annex-target.md) §C5/§C7。

## §1.2 Document（Kotlin resolve 产物）

- **三段**：① 数据面（HOCON 合并后）· ② 计划（Lua 文本 + 大小 + sha256）· ③ 收尾 payload（脚本/LKM，bin + sha256）。
- **执行**：root 前 stage 一次 ⇒ gate 校验 staged ⇒ 按相位执行 ⇒ 按 ③ 段收尾；**零隐性回退**。
- **三条推论**（详见 [Document 附件](lua-surface-annex-document.md)）：① 脚本控制流属 document，禁 runtime 自加回退 · ② 换路须写进 document · ③ `run` 随 ③ 段移除。

## §2 绑定权威（**单一名单**）

- **持有者**：`surface` 表（`constexpr`）= 唯一权威；宿主按表注册（集合 ≠ 表 ⇒ 启动期失败）。**机器判据**：① 注册集合 == 表；② 条目名只定义一次；③ 沙箱符号级证据。
- **判据形式**：表外调用 ⇒ **具名拒绝** `surface-unknown-name`（**默认拒绝**，不做近似匹配）。
- **一致性**：表与绑定同源；「先看绑定再补表」= 违例。

## §3 单遍执行（**跑脚本 → stage → gate 校验 → 按相位执行**）

1. **跑脚本**：窗口外一次 ⇒ staged 计划。
2. **gate 校验（对象 = staged）**：十条 + surface 表；失败 ⇒ 具名拒绝。**3. 执行**：按 staged；脚本不参与。

- **逐调用边界校验**：校验参数界/句柄/相位；越界 ⇒ 具名拒绝。
- **判据（三条，可证伪）**：
  - **stage 零副作用**：stage 期无写系统调用/资源获取（证据 = 符号级 + 观测）；
  - **确定性**：两次 stage 逐字节相同；
  - **staged == 执行**：逐条逐值一致；偏差 ⇒ 终止。

## §3.1 纪律放宽（**TD-RW2-1**）

- 「**用户脚本正确性 = 用户责任**」⇒ 不做脚本逻辑静态证明；**保留**窗口内零 Lua · 逐调用校验 · gate 校验 staged · 预算；**代价**改坏可能 panic；**偿还 = 首个 G2 后复评**（见 [待批单](../plan/A6-baseline-approval.md) §5）。

## §4 资源与窗口

- **句柄边界**：脚本只持表内句柄；POD `{kind,index,generation}` · 有界 64 · 无 `__gc`（[ADR-0010](../analysis/adr/0010-lua-cpp-resource-model.md) 待批）。
- **窗口内**：脚本**不执行**（零 Lua）；窗口相位动作由实现层按 **staged** 执行；其余条目不得出现。
- **资源归属**：窗口资源由窗口宿主独占（RAII 回收）；缺参数 ⇒ 具名拒绝。

## §4.1 三层收尾（声明式）

- **收尾三声明**：`terminal{on_success/always/on_failure}`（声明式）。
- **内核兜底留 C++**：relay/enforce 恢复 + watchdog。
- **被取代（沿革不删）**：旧 `root_cmd` 模板被取代。

## §4.2 handoff 归属（Kotlin 负责）

- HOCON 只声明意图；Kotlin `resolve` 产 payload ⇒ wire ⇒ native 校验+落盘 ⇒ terminal 执行。

## §6 待裁

| # | 待裁 | 选项 | 默认 |
|---|---|---|---|
| **Q1/Q2** | `settle` 进窗口 · `width` 缺省 | ① 允许 / ① 即拒 | **①** |
| **Q3** | 脚本承载 | ① 资产 ② 内联 | 待定 |
| **Q4** | 换路用例 | 有 / 无 | 待裁 |
| **Q5** | 自动回退 | ❌ 与禁回退冲突（需强制越权） | 待裁 |
| **Q6** | `prepare{}` 要不要 | ① 不要【Lead 倾向：构造=选实现，其余在机制层】② 要（须定义 做什么/相位/绑定） | 待裁 |
| **Q7** | per-attempt 机制（drain/spray/竞争/退避）留机制层 | ① 同意（参数走 profile）② 不同意 | 待裁 |
