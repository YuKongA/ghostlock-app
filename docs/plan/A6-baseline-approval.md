# A6 基线待批单（一页 · 2026-10-08）

> **状态：已批准**（2026-10-08 · 用户原话「计划全部批准，开始写代码」；审查：v0.2 两轮，见 `engineering-rules.md` 审查记录区）。
> 用途：把 **A6（Lua 脚本计划）开工所需的全部批准项**收在一页；**批准后即可进入 A6 实现批**。计划类（≤16 KiB）。
> 权威件：[lua-surface-design](../analysis/lua-surface-design.md) · [ADR-0008/0009/0010](../analysis/adr/) · [rewrite-plan-v2](rewrite-plan-v2.md)（A6）· [PENDING-DECISIONS](PENDING-DECISIONS.md)。

## 1. 待批内容（8 项）

| # | 项 | 内容 | 权威件 |
|---|---|---|---|
| 1 | **引擎与 vendoring** | **Lua 5.5.1** vendored 到 `src/lib/lua/`（`PROVENANCE.md`：398 643 B · sha256 前 16 `1c4b4068…` · 63/63 逐字节相同）；体积 **≈ 119 000 B ≈ 2.40%**（本仓 lua-core 口径：纯 -Os；含 -DLUA_USE_POSIX 为 119 068 B，对照见 PROVENANCE.md） | ADR-0008 |
| 2 | **沙箱预算参数** | **不注册** `io`/`os`/`package`/`debug`/`load`/`dofile`/`require`；指令预算 · 内存硬上限 · 栈深 · **单脚本 ≤64 KB** · 脚本**只在窗口外**运行（**5.5 API 逐项复核 = 待办，A6 批内**）· **白名单装配 = 不调用基础库注册器**（`luaopen_base`/`luaL_openlibs` 均不调用；改逐项 `lua_setglobal` 注册 ⇒ `load`/`loadfile`/`dofile` **运行期不存在**；证据与结论见 [ADR-0008](../analysis/adr/0008-lua-runtime-sandbox.md) §背景 3.1） | ADR-0008 · P5 规格 |
| 3 | **暴露面表（写入面两形态）** | `backend(id)` · **`b:writeop{target,value,width}`（字写；**target = {kind, value}**：kind = 机制枚举、value **来自 `cfg`**）** · **`b:copyop{src,dst,width}`（结构复制；src/dst 同为目标形态）** · `b:settle{us}` · **`b:handoff{terminal,payload_ref}`（引用语义）** · `cfg`（只读快照） · **`close(h)`（句柄释放）**；**暴露面 7 条**；表外 ⇒ `surface-unknown-name` | [lua-surface-design](../analysis/lua-surface-design.md) §1/§1.1 |
| 4 | **单遍 stage** | 跑脚本（窗口外一次）⇒ **staged 计划** ⇒ `plan_gate` **校验 staged 产物**（不重跑脚本）⇒ **按相位执行**；**逐调用边界校验保留** | 同上 §3 |
| 5 | **`cfg` 快照** | stage 前注入的**只读快照**；脚本只能读字段；**不属句柄面**、不产生句柄 | 同上 §1 · ADR-0010 |
| 6 | **句柄边界 + 禁止名字→值表** | `{kind,index,generation}` · 有界 64 · POD/无 `__gc` · 只由宿主探针产生 · 不得自造/延长/跨域；**C++ 不得持「名字→值」表**（名字只允许 dispatch 标识符与数据面键路径）⇒ [target 附件](../analysis/lua-surface-annex-target.md) | ADR-0010 · 附件 |
| 3b | **`width` 必填 + 相位四词（A1）** | **`writeop`/`copyop` 的 `width` 必填**（**不推断**）；**profile 声明的结构尺寸 = 校验依据**，不匹配 ⇒ 具名 **`width-mismatch`**；**相位用 manifest 四词表**：`stage` · `pre_window` · `window` · `terminal`（backend/close=`pre_window` · writeop/copyop/settle=`window` · handoff=`terminal` · cfg=`stage`） | [lua-surface-design](../analysis/lua-surface-design.md) §1.1 · [manifest 附件](../analysis/lua-surface-annex-manifest.md) |
| 3c | **manifest 对拍（新判据）** | **暴露面单一权威 = `make -C src surface-manifest` 导出 + pin**（`src/core/tests/surface_manifest_test.cpp`）；**对拍 = `make -C src surface-doc-check DOC=<文档>` 必须 EXIT=0**（**两份**：[UML](../development/full-process-uml.md) 与 [manifest 附件](../analysis/lua-surface-annex-manifest.md)） | 同上 |
| 3d | **`holds_resource` 语义（A3）** | **语义 = 调用返回后仍使资源存活** ⇒ **`backend`=true · `handoff`=true** · **`close`=false** · 其余 false（**manifest col5 实测**：`surface_manifest_test.cpp:6` 的列序 = `…phase·**holds_resource**·recordable·executable…` ⇒ **第 5 列**）；**已由 `resource_semantics_are_pinned()` 钉住**（`script_surface.hpp:541` `backend && !close && handoff`；`:644` static_assert 同义） | manifest col5 / `script_surface.hpp` |
| 7 | **三条 ADR 修订点** | 0008 = 单遍 stage + 5.5 沙箱参数待办 · 0009 = 「用户脚本正确性 = 用户责任」= **`TD-RW2-1`** · 0010 = **`cfg` 不属句柄面** | 三份 ADR 修订段 |
| 8 | **handoff 归属（Kotlin 负责）+ 收尾 + route 换法** | HOCON 只声明意图；**Kotlin `resolve`** 产 payload（收尾脚本 + LKM + 大小 + sha256）⇒ wire 传入 ⇒ native 校验 + 落盘 ⇒ terminal 执行（声明式三段；内核兜底留 C++）；`run` **已移除**（**`TD-RW2-2` 作废**）；**route 换法 = 方案 2（`close` + 重新 `backend(id, {route=…})`）**，同 backend 双活跃句柄 ⇒ **`handle-conflict`** | [lua-surface-design](../analysis/lua-surface-design.md) §1/§4.2 · [route 附件](../analysis/lua-surface-annex-route-switch.md) |

**沙箱白名单（逐项，供批准/微调；装配方式 = 白名单装配，不调用基础库注册器）**

| 类别 | 条目 | 说明 |
|---|---|---|
| **保留** | `assert` · `error` · `type` · `tostring` · `tonumber` · `pairs` · `ipairs` · `next` · `select` · `pcall` · `xpcall` · `rawget` · `rawset` · `rawequal` · `rawlen` | **纯语言语义**：无 I/O、无装载、无元表逃逸 |
| **排除** | `load` · `loadfile` · `dofile`（ADR 明令） · `print`（写 stdout，属 I/O；诊断改走暴露面审计） · `warn` · **`setmetatable`/`getmetatable`**（元表是经典逃逸面；句柄方法调用不需要元表） | — |
| **保留但受预算约束** | `collectgarbage`（脚本可主动回收；**是否暴露 = 待裁项**） | 受指令/内存预算约束 |

## 2. 现状（2026-10-08）

- **已就位**：`src/lib/lua/`（5.5.1 + `PROVENANCE.md`）· 暴露面设计与 UML（§2.8/§3.4/§4.5 已同步 stage/相位/cfg）。
- **未开始**：A6 实现（沙箱接线、`surface` 表、stage、gate 校验 staged、句柄表）。
- **前置未决**：待裁三条（`settle` 进窗口 / `width` 缺省 / 脚本承载方式）+ **Q6/Q7**（`prepare{}` 要不要 · per-attempt 机制留机制层）；**5.5 API 逐项复核**（沙箱参数，A6 批内做）。

## 3. 判据（批准后 A6 的验收）

- **白盒（入口/期望）**：`make -C src surface-doc-check DOC=<文档>` ⇒ **EXIT=0**（两份）；`make -C src native-host-tests` ⇒ **EXIT=0**；沙箱**符号级证据**（产物无 `io/os/package/debug/load/dofile/require`）；`surface` 表与绑定**同源**（注册集合 == 表；名字只定义一次）；句柄表**有界 64 + 代际**。
- **黑盒**：表外名字 ⇒ `surface-unknown-name`；缺参数 ⇒ 具名拒绝（**不兜底**）；staged 计划被篡改 ⇒ gate 拒绝。
- **证伪（两条，按 R42 执行；补可执行对象）**：① 放开 `load`/`dofile` 之一 ⇒ **沙箱测试**（A6 首批建 `src/core/tests/surface_sandbox_test.cpp`）**必须失败**，入口 `make -C src check`，**期望退出码 ≠ 0**；② 去掉代际检查 ⇒ **句柄代际用例**（A6 首批建 `src/core/tests/handle_generation_test.cpp`）**必须失败**，同入口、**期望退出码 ≠ 0**。**控制项**：未造错时二者**必须 EXIT=0**。**若测试在 A6 首批才建 ⇒ 首个 A6 批建测试并回填本表**。
- **极端值**：空脚本 · 语法错 · 超预算 · 表超深 · 64 句柄用满 · 跨插件句柄 · 脚本内 `queue` 残留。

## 4. 不批的后果

- **A6 无法开工**（沙箱与暴露面同源绑定、句柄模型都是实现前提）⇒ **P5 相关项全部顺延**；
- `plan_gate` 无 staged 对象可校 ⇒ **窗口相位执行缺乏校验依据**；
- 引擎版本/vendoring 虽已裁定，但**沙箱参数未批** = 不得接线 ⇒ A6 只能停在"源码就位"状态。

## 5. `TD-RW2-1` 登记（纪律放宽）

| 项 | 内容 |
|---|---|
| 放宽 | **不为脚本逻辑错误做静态证明**（用户裁定：**用户脚本正确性 = 用户责任**） |
| **保留** | **窗口内零 Lua** · **逐调用边界校验** · **`plan_gate` 校验 staged** · **预算**（指令/内存/64 KB） |
| 代价 | 用户脚本改坏**可能 panic**（本层不拦截，如实登记） |
| **偿还时机（A14）** | **首个真机门禁（G2）后复评**（届时按真机证据决定是否加回部分静态检查） |

- **可检索记录（AGENTS 记录义务）**：`2026-10-08 · AGENTS「验证门槛/R10」· 结论：放宽并登记 TD-RW2-1`（同批留沿革 + 技术债）。

**另：`TD-RW2-2`（`run` 双形态）＝ 已作废** —— 用户 2026-10-08 裁定移除 `run`（收尾命令定义权归 Kotlin，见 §1 第 8 项）⇒ 该债随之失效：**暴露面不再有执行命令的能力**，故「命令级白名单失效 / root 后可执行任意命令」的代价**不再存在**。沿革保留（不删历史）：该债曾因「允许 `run{shell}`」而立，随能力移除而关闭。

## 6. 一句批准项（可直接转给用户）

> **请批准 A6 基线（新口径）**：**Lua 5.5.1**（已 vendor、`PROVENANCE.md` 可核）· **沙箱 = 白名单装配**（不调用 `luaopen_base`/`luaL_openlibs`）· **暴露面 7 条**（`width` **必填**；`handoff` 带 **`payload_ref`**；**相位四词** `stage`/`pre_window`/`window`/`terminal`）· **单一权威 = `surface-manifest` 导出 + pin**（对拍 `surface-doc-check` 两份 **EXIT=0**）· **`holds_resource`**（**manifest col5**）：**`backend`=true · `handoff`=true · `close`=false**（其余 false；**已具名断言** `resource_semantics_are_pinned()`）· **`TD-RW2-1`**（放宽 + 保留四项 + 偿还 = 首个 G2 后复评）+ **沙箱预算参数**（不注册 `io`/`os`/`package`/`debug`/`load`/`dofile`/`require`；指令/内存/栈深预算；单脚本 ≤64 KB；脚本**只在窗口外**运行）+ **暴露面 7 条**（`backend(id,{route?})` · **`close(h)`** · `writeop{target={kind,value},value,width}` · **`copyop{src,dst,width}`** · `settle{us}` · **`handoff{terminal,payload_ref}`（引用语义）** · `cfg`（只读快照）；**表外 ⇒ `surface-unknown-name`**）+ **单遍 stage**（跑脚本 → `plan_gate` 校验 staged → 按相位执行）+ **句柄边界**（有界 64 · POD/无 `__gc` · 仅宿主探针产生 · 代际检查 · `handle-conflict`/`handle-stale`）+ **C++ 不得持「名字→值」表**（名字只允许 dispatch 标识符与数据面键路径）+ **收尾归 Kotlin**（resolve 产 payload ⇒ wire ⇒ native 校验落盘 ⇒ terminal 执行；内核兜底留 C++）+ **route 换法 = 方案 2**（`close` + 重新 `backend(id,{route})`），并登记 **`TD-RW2-1`**（用户脚本正确性 = 用户责任；保留窗口内零 Lua / 逐调用校验 / gate 校验 staged / 预算）。**（`TD-RW2-2` 随 `run` 移除作废。）**
