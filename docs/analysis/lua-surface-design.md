# Lua 暴露面设计（绑定面 = **实际暴露什么就绑什么**）

> **表达一致**（用户 2026-10-08）：同一件事**只允许一种表达**，不得「首设用 A 语法、后续用 B 语法」分裂（例：route **只在 acquire 给**，换路 = `close` + 重新 acquire）。
>
> **禁止 C++ 名字→值表**（名字只允许 ① dispatch 标识符 ② 数据面键路径）⇒ [target 附件](lua-surface-annex-target.md)。
>
> 分析类（≤8 KiB）。**自包含**。**口径（用户 2026-10-08 修正）**：绑定面**不是**「只能 `contract`」，而是「**我们实际暴露什么就绑什么**」⇒ 先有**暴露面表**（唯一权威），再按表绑定；**表外名字 ⇒ 具名拒绝**。
> 相关：[ADR-0008](../analysis/adr/0008-lua-runtime-sandbox.md) · [ADR-0010](../analysis/adr/0010-lua-cpp-resource-model.md)（**待批**）· [P5 规格](../analysis/handoff-script-layer-p5-spec.md) · [rewrite-plan-v2](../plan/rewrite-plan-v2.md)。

## §1 暴露面表（**唯一权威**；表外名字一律拒绝）

| 条目 | 形态 | 实现层 | 相位 |
|---|---|---|---|
| `backend(id, {route?})` | call | `backend` | **`pre_window`** |
| **`close(h)`** | handle_method | **`session`** | **`pre_window`** |
| `b:writeop{target, value, width}` | handle_method | `backend` | **`window`** |
| **`copyop{src,dst,width}`** | handle_method | `backend` | **`window`** |
| `b:settle{us}` | handle_method | `backend` | **`window`** |
| `b:handoff{terminal, payload_ref}` | handle_method | `terminal` | **`terminal`** |
| **`cfg`**（只读快照） | value | `profile` | **`stage`** |
| ~~`run`~~ | — | — | **已移除**（属 ③ 段） |
| 语言级循环/条件 · 表外名字 | 语言结构 / — | `script` / — | 窗口内零 Lua / **拒绝** |

- **语义与拒绝码**（逐条）：见 §4/§4.1/§4.2 与 **UML §3.2 唯一码表**；**相位列 = manifest 四词表**（`stage` · `pre_window` · `window` · `terminal`）。

- **口径（实现层）**：**实现层 = `contract::kLayers` 的层名**（10 个；见 [`script_surface.hpp`](../../src/core/contract/script_surface.hpp) 的 `implements_layer` 与层级断言）；**机器可校验**，**不得用描述性短语** —— 「**表达一致**」的一个实例。
- **列语义**：stage/执行 = 单遍两段是否放行；相位 = 内部；持资源 = 否（**实现层口径见下**）。
- **窗口级 op 已降为内部**：脚本不表达窗口分组。

- **不进表的量**（驱动器参数 · route 几何 · 机制常量 · 运行期量）⇒ 见 [target 附件](lua-surface-annex-target.md) §C5/§C7。

### §1.1 派生源与验收

- **派生源**：本表由 `make -C src surface-manifest` 导出；**原文 19 行**见 [manifest 附件](lua-surface-annex-manifest.md)。
- **验收**：`make -C src surface-doc-check DOC=docs/analysis/lua-surface-annex-manifest.md` **必须 EXIT=0**（逐行对拍；pin = `src/core/tests/surface_manifest_test.cpp`）。

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

- **`payload_ref` 语义**：**document ③ 段（payload 段）内声明的交付物标识**（代码头注：`script_surface.hpp:59-61`「names the payload declared in the document's payload section」）；**解析点 = handoff 校验**；**无此标识或校验失败 ⇒ 具名 `payload-missing`**。

- HOCON 只声明意图；Kotlin `resolve` 产 payload ⇒ wire ⇒ native 校验+落盘 ⇒ terminal 执行。

## §6 待裁

| # | 待裁 | ① 选项 | ② 选项 | 默认（含义写全） |
|---|---|---|---|---|
| **Q1** | `settle` 是否可进窗口 | ① 允许（`us` 受 profile 上界约束） | ② 仅窗口外 | **① = 允许进窗口**（值必须来自 `cfg`） |
| **Q2** | `writeop`/`copyop` 的 `width` 缺省 | ① 缺省即拒（`param-missing`） | ② 从 profile 取 | **① = 缺省即拒**（与「数值不入 C++」一致；A1 已裁定） |
| **Q3** | `available` 项脚本的承载 | ① **资产路径**（Kotlin resolve 时读资产、随 document 传入） | ② 内联（脚本正文直接进 HOCON/文档） | **① = 资产路径**（二选一已定；与 UML/待批单 item 3 同步） |
| **Q4** | 是否有中途换路的真实用例 | ① 有（按 [route 附件](lua-surface-annex-route-switch.md) 方案 2 实现） | ② 无（不做该能力） | 待用户回答 |
| **Q5** | 是否允许自动回退 | ① 不允许（现裁定：与「禁止运行时回退」冲突） | ② 允许（须**用户明确改口 + 强制越权登记**） | **① = 不允许**（保持现裁定） |
| **Q6** | 是否要 `prepare{}` | ① 不要（构造 = 选实现，其余在机制层） | ② 要（须同时定义 做什么/相位/与后续 `writeop` 的绑定） | **① = 不要**（Lead 倾向） |
| **Q7** | per-attempt 机制（drain/spray/竞争/退避）是否留机制层 | ① 同意（参数走 profile） | ② 不同意（提到计划层） | **① = 同意** |

> **2026-10-08 批准**：Q1/Q2/Q3/Q5/Q6/Q7 按表中默认执行；A13（`settle` 去留）按『**保留**』执行（用户批准原文）。
