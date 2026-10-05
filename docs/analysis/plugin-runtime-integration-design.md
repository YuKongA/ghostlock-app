# 插件运行时接线设计（P1 之后：加载 → 按 stage 调用 → 卸载）

> 状态：**设计草案（L 级，待 Lead 评审）**。本文只做设计，不含实现。
> 依据：`docs/analysis/contract-design.md` §3.13 / §3.14（P1 冻结）、`docs/development/design-philosophy.md`、AGENTS.md「核心攻击代码审查」。
> 关联：P1 已完成探针（`--plugin-probe`）、ABI 尾部追加、`plugin.*` FieldSpec/manifest、`plugin/wire.cpp` 的 fail-closed 校验；δ-4 已有 loader/registry 与 LKM 窗口内的 POST_TERMINAL 试验通道。

## 1. 目标与非目标

**目标**：把「已导入并被文档启用」的插件接进生产流水线：按 `plugin.<id>.stage` 在正确的阶段加载、调用其 hook、在正确的边界卸载；全过程 fail-soft、可诊断、可审计。

**非目标**（本设计不覆盖，留给后续批次）：
- 插件参数/提取值的**运行时读取 API**（`RuntimeInfo`，属 P3）；
- extractor 投影（P2）；
- 插件市场/签名体系；
- 多插件之间的依赖或排序（仅按 `priority` + 注册顺序）。

## 2. 现状与接缝

| 组件 | 现状 | 本设计中的角色 |
|---|---|---|
| `plugin/loader` | 白名单根 + realpath/`..` 校验 + SHA-256 固定 + ABI 校验 | **唯一**的 dlopen 路径，插件宿主只用它 |
| `plugin/registry` | `RuntimeRegistry::reset(host_caps, host_triggers)` + `register_module` + 按 stage 派发 | 每插件实例的注册表；能力/阶段不足在此拒绝 |
| `plugin/controller` | 阶段派发骨架 | hook 调用的执行点 |
| `backend/.../lkm_window` | 43284 的 LKM 驻留窗口；δ-4 在窗口内派发 POST_TERMINAL | 43284 上 `post_terminal` 的宿主窗口 |
| `plugin/wire` | `plugin.*` 的 fail-closed 校验（P1） | 决定「哪些插件、什么阶段、什么参数」 |
| `pipeline/orchestrator` + `Pipeline<Backend,Terminal>` | 后端步骤 → 终端接管 | 阶段边界的**唯一**知情者 |

## 3. 阶段语义与触发点（**最初假设，已被 §12 的代码核实表修正；冲突时以 §12 为准**）

阶段词汇冻结为 `pre_spawn | post_spawn | pre_terminal | post_terminal`（`pre_route` 保留、注册即拒）。

| stage | 触发点（生产流水线内） | 与 root_child / UMH 的先后 | 语义与允许能力 |
|---|---|---|---|
| `pre_spawn` | 43499：route/race 完成且 W1（SELinux permissive）成功之后、W2/W3 之前 | 早于 root child 存在 | 只允许「改内核状态」类：`kernel_read`/`kernel_write`/`alias`；**不得**依赖 `child_task` |
| `post_spawn` | root child 已存在（`child_task` 有效）之后 | 紧随 root child 建立 | 可用 `child_task`；仍不得触碰终端接管 |
| `pre_terminal` | 终端接管前的最后一个可回退点 | 紧邻 root child 接管/UMH 触发之前 | 可改内核状态；不得阻塞终端 |
| `post_terminal` | 终端完成之后（43284：LKM 驻留窗口内；43499：root 已接管） | 晚于接管 | 43284 上唯一拥有 `/dev/glk` 能力（KernelMemory/KernelAlias）的窗口 |

**硬约束**：阶段边界由 `Pipeline` 调用，插件**不得**自行决定阶段；`trigger != ON_STAGE` 的 hook 在注册期即被拒（P1 已实现）。

## 4. 生命周期与所有权（与 PI race/waiter 的关系）

**规则 R1（不可协商）**：任何插件 `.so` 的映射**不得**在 PI waiter 存活期间存在。

- waiter 的生命周期只覆盖后端 route/race 阶段（`Route::prepare → execute → disarm → destroy` 内的竞争窗口）；
- 因此插件宿主的 `load()` **必须**发生在「后端报告等待窗口已关闭」之后（43499：route 完成且 `userspace_clean`；43284：无 waiter，按 pagecache 写入前的固定点）；
- `load()` 之前的所有阶段派发请求一律视为「未加载 → 跳过并记账」，绝不隐式触发加载；
- `unload()` 发生在流水线作用域退出（成功/失败/提前返回/异常路径统一），43284 上必须在 LKM 窗口关闭后。

**所有权（RAII）**：

- `PluginHost` 由组合根（`main.cpp`）在文档校验通过后构造，**按值**持有于 `run_orchestrated_pipeline` 的调用作用域；
- `PluginHost` 拥有：已启用插件的 `Loader` 句柄、每插件的 `RuntimeRegistry`、参数视图、诊断计数；
- `Pipeline` 只持有 `PluginHost*`（借用，非拥有），生命周期由组合根保证长于流水线调用；
- 析构顺序固定为：`close LKM window`（43284）→ `unload each module` → 释放参数缓冲；**不反转**（与 AGENTS「终结点与清理顺序」一致）；
- 插件**不得**持有宿主指针跨越调用返回（`glk_contract_ops.ctx` 仅在调用期有效，调用返回后宿主可使句柄失效）。

**UAF 检查清单（实现时逐条核对）**：注册表引用的 `glk_module` 表指向 `.so` 镜像 → 卸载前必须停止一切派发；参数视图指向文档缓冲 → 其生命周期必须覆盖 `PluginHost`；`child_task` 等运行时值按调用传入，不缓存。

## 5. 失败语义（fail-soft + 记账）

| 事件 | 处置 | 攻击链 |
|---|---|---|
| 文档里启用了插件但 `.so` 不存在 / 哈希不符 / ABI 不符 | **加载失败**：不注册，记 `plugin=<id> stage=… error=<LoadStatus>`；**不终止攻击链** | 继续 |
| 注册期拒绝（能力/阶段/trigger 不足） | 同 loader/registry 既有语义，记拒绝原因 | 继续 |
| hook 返回非 0 / 抛出（C ABI 无异常，按返回值） | 记 `hook_failed=<name> rc=<n>`，该插件**后续阶段全部跳过**（不重复调用） | 继续 |
| hook 之间互相影响 | 宿主不保证隔离；优先级升序 + 注册顺序执行 | 继续 |
| 插件返回值 | **一律不可信**：宿主不据此改变控制流、不放权、不跳过任何验证 | 继续 |

**原则**：插件的存在只能**增加**动作，不能**减少**任何安全检查；任何插件故障都不得让攻击链失败，也不得让宿主放宽既有校验（与「fail-closed 的是检查，不是可选增强」一致）。

## 6. 安全边界

- 路径：只接受来自 `plugin.<id>.module_path` 的**相对**路径，根 = `<GHOSTLOCK_HOME>/countermeasures`（P1 已定案）；绝对路径/`..`/反斜杠在 `plugin/wire.cpp` 即拒；
- 哈希：`module_hash` 必须在 dlopen 前与文件实算 SHA-256 一致（复用 loader 判定顺序；`support::sha256_file` 为唯一实现）；
- 白名单根 + realpath 二次校验（loader 既有）；
- 能力：注册期按 `glk_module.required_caps` 与 host 实现集比对，保留位拒绝；阶段同理；
- 不放权：宿主永不让插件决定「是否启用/是否失败/走哪条 route」；`enabled` 只来自文档（默认关闭，P1）；
- 审计：实际生效的插件与参数进诊断（R5 同规），**不落盘密钥**。

## 7. 接口草案（实现批次用）

~~~cpp
// plugin/host.hpp（草案）
namespace ghostlock::plugin {
    enum class HostStage : uint8_t { PreSpawn, PostSpawn, PreTerminal, PostTerminal };

    struct HostDiagnostics final {   // 只记账，不改变控制流
        uint32_t loaded = 0, load_failed = 0, rejected = 0, called = 0, hook_failed = 0, skipped = 0;
    };

    class PluginHost final {         // RAII；组合根按值持有，Pipeline 只借用
    public:
        // 文档校验通过后构造；此时**只登记**，不 dlopen
        static PluginHost from_document(const profile::Document &, const char *ghostlock_home);
        // waiter 窗口关闭后由后端/流水线调用一次；失败 fail-soft
        void open() noexcept;
        // 阶段派发；未 open 或该插件无此 stage → 计数并返回
        void dispatch(HostStage stage, const contract::PluginCallContext &) noexcept;
        // 作用域退出：先关 LKM 窗口再卸载
        void close() noexcept;
        ~PluginHost();               // = close()
        PluginHost(const PluginHost &) = delete;
        const HostDiagnostics &diagnostics() const noexcept;
    };
} // namespace ghostlock::plugin
~~~

调用点（草案）：`pre_spawn`/`post_spawn`/`pre_terminal` 在 43499 后端步骤与终端边界处由 `Pipeline` 调 `host.dispatch(...)`；`post_terminal` 在 43284 的 LKM 窗口内由窗口回调调；每个调用点只传**值**，不传宿主内部对象。

## 8. 测试与真机门禁设计

**host 测试（无设备、无真 .so 依赖，用 `plugin/controller` 的注入面 + 测试 .so）**：
1. 未启用/加载失败 → 攻击链路径不变（用 fake backend/terminal 断言调用序列一致）；
2. 阶段顺序：`pre_spawn → post_spawn → pre_terminal → post_terminal` 严格升序、每插件每阶段至多一次；
3. hook 返回非 0 → 该插件后续阶段跳过，其它插件不受影响；
4. `open()` 前的 dispatch 全部计为 skipped（不隐式加载）；
5. 生命期：`close()` 后不再有任何派发；`.so` 校验失败时**未发生 dlopen**（计数为 0）；
6. 能力/阶段不足的插件在注册期拒绝且不调用其 hook；
7. 参数/哈希/路径负例沿用 P1 的 `plugin_wire_test` 矩阵。

**真机门禁（唯一权威判据，冷机 + KernelSU 未加载）**：
- 正例：文档启用一个真实插件（P3 参考插件或 delta-4 试验 .so）→ 43499 冷启 PASS，日志出现 `plugin=<id> stage=<s> called=1`，AVB 12/0；43284 app-call PASS（`post_terminal` 在 LKM 窗口内被调用）；
- **负例 A（故意失败）**：插件 hook 返回非 0 → 日志记 `hook_failed`，**攻击链仍 PASS**（uid0/KernelSU 就绪），AVB 12/0；
- **负例 B（制品被篡改）**：改了 `.so` 一字节但文档哈希未更新 → `HashMismatch`，**不 dlopen**，攻击链仍 PASS；
- **负例 C（waiter 窗口保护）**：日志/断点证据表明 `.so` 映射发生在 route 完成之后（同一日志里 `route=ok` 早于 `plugin loaded`）；
- 归档：`docs/analysis/device-gates/plugin-runtime-<date>-pass.md`，含上述四条的原始日志与退出码。

## 9. 落地顺序与回滚

1. **本设计评审**（Lead 认可后进入实现）；
2. `plugin/host.{hpp,cpp}` + host 测试（不接流水线，纯单元）；
3. `Pipeline` 三处调用点 + 组合根接线（43499 先、43284 后）；
4. 真机门禁（含负例 A/B/C）→ 归档；
5. 回滚：`PluginHost` 不接线即回到现状（`enabled=false` 默认关闭，wire 里没有插件段 → 行为与今天完全一致）。

## 10. 开放问题（请评审时裁决）

1. `pre_spawn` 的精确位置：W1 之后 / W2 之前 是否就是维护者要的语义？（备选：route 完成即触发）
2. 43499 的 `post_terminal` 在 root 已接管后由谁触发（root child 进程内无法回传宿主）——建议**不实现**，只在 43284 的 LKM 窗口内提供；需要在文档里写明该阶段对 43499 不可用。
3. 多插件优先级冲突时是否需要「互斥声明」（如两个插件都要 `kernel_write`）——建议 P3 再议。
4. 插件参数在调用期如何暴露（`glk_contract_ops.query_*` 的路径白名单内容）——与 P3 的 `RuntimeInfo` 一起定。


## 11. 评审结论落实（2026-10-05，Lead 5 条条件）

### 11.1 pre_spawn 的调用点：已核实的部分 + 待钉的一步

评审要求把 `pre_spawn` 写成经代码核实的调用点，而不是阶段名；原则是「(a) waiter 窗口已关闭 **且** (b) 已具备内核写能力」的第一个点。

**已核实（file:line，本批只读勘察）**：

- `src/core/backend/cve_2026_43499_backend.cpp:111` `run_setup(...)`（middleware-free 的 setup/profile 安装）；
- `src/core/backend/cve_2026_43499_backend.cpp:121` 起 `StepSet::run<Route>(...)` —— **route/race 与 W1/W2/W3 全在这一个调用内部**；
- `src/core/backend/cve_2026_43499_backend.cpp:138-158` 才把 rooted child 移交给终端（`out.alive` / `release_child`）。

**结论（修正原来「W1 之后 / W2 之前」的表述）**：由于 route/race 与 W1 同处 `StepSet::run<Route>` 内部，`pre_spawn` **不能**在 `Pipeline` 层表达；它的候选锚点只有两个：

1. `src/core/backend/cve_2026_43499/steps.cpp:386` 的 `w1(...)` 调用**之前**且 route 返回 Ok 之后——即「waiter 窗口已关闭、但内核写能力尚未取得」；
2. `w1(...)` 返回成功（SELinux permissive）之后、`w2(...)`（`steps.cpp:157`）之前——即「窗口已关闭 + 已具备内核写能力」。

按评审定义的原则，**方案 2 才是 `pre_spawn`**（(a)+(b) 同时成立）。`steps.cpp` 内 `w1/w2/w3` 的**调用点行号**尚未钉死（需完整读 `steps.hpp` 的 `run<Route>` 模板体），因此本条**未闭合**：实现批次（step 2）的第一步就是把该调用点写成 `file:line` 并回填本节，再动 `plugin/host.*`。

### 11.2 43499 的 post_terminal：文档化不可用（采纳）

- 设计侧已在 §3 表内写明：`post_terminal` 仅在 **43284 的 LKM 驻留窗口**内提供；43499 上 root 已接管、无法回传宿主，**不提供**该阶段；
- **需 docs-uml 同步到 `docs/analysis/contract-design.md` 与 `docs/development/full-process-uml.md`（我不跨流写）**：
  1. `contract-design.md` §3.14.7 增一条：`post_terminal` 对 43499 **不可用**（不是沉默缺席）；
  2. `full-process-uml.md` §2 状态机：43499 链上不出现 `post_terminal` 分支；43284 链的 `post_terminal` 明确画在 LKM 驻留窗口内；
  3. `full-process-uml.md` §1 IPO：终端阶段（C6）之后仅 43284 有插件派发列。

### 11.3 开放问题收敛

- §10 第 3 问（多插件优先级冲突/互斥声明）与第 4 问（调用期参数查询白名单）**延到 P3**，本设计不做决定；
- §10 第 1 问由 11.1 取代（不再是「W1 之后/W2 之前」的措辞问题，而是待钉的调用点）；
- §10 第 2 问已按建议采纳（11.2）。

### 11.4 用户同意边界（新增，评审第 4 条）

在生产进程内运行插件 = 允许**与攻击进程同等权限的任意代码**在同一地址空间执行。因此边界必须是显式且可审计的：

| 维度 | 规则 |
|---|---|
| 显式导入 | 只能由用户在 App 内主动选择文件导入（P1 导入流程）；**绝不**自动扫描目录或自动发现 |
| 显式启用 | 只有文档里 `plugin.<id>.enabled = true` 才加载（默认关闭；`enabled=false` 出现在文档中一律拒绝，P1 `plugin/wire.cpp` 已实现） |
| 设备本地制品 | 插件只来自设备本地导入的 `.so`；**永不随包内置**、永不从网络下载 |
| 哈希钉住 | `module_hash` 在 dlopen 前逐字节校验（loader + `support::sha256_file`）；哈希不符**不加载** |
| 权限不提升 | 插件与攻击进程同权限，宿主**不**授予任何额外内核能力：能力仍由 `required_caps ∩ host_impl` 决定，保留位拒绝 |
| 可撤销 | 删除设备上的制品 / 置 `enabled=false` 即回到无插件行为；宿主不缓存、不驻留 |

用户可见文案（Kotlin 半场）必须表达「该插件将与攻击进程同等权限运行」，本设计只固定 native 侧的边界语义。

### 11.5 open() 的窗口前置断言（新增，评审第 5 条）

R1（插件映射不得存在于 PI waiter 存活期）必须有**可执行证据**，因此：

- `PluginHost::open()` 的签名增加前置状态参数（或等价的状态查询）：`open(WindowState)`，其中 `WindowState ∈ {WaiterAlive, WaiterClosed}`；
- `open(WaiterAlive)` → **拒绝并记账**（`open_rejected=1`），不 dlopen、不注册，返回 false；
- debug 构建下同时 `assert(window == WindowState::WaiterClosed)`，让违反 R1 的调用在测试期立刻暴露；
- host 测试必须覆盖：① `open(WaiterAlive)` 拒绝且 open 计数为 0；② `dispatch()` 在 `open()` 之前全部计 skipped；③ `close()` 之后不再有任何派发；
- 真机门禁负例 C 的证据链：日志中 `route=ok`（窗口关闭）必须**早于** `plugin loaded`。

### 11.6 实现批次（step 2）的准入条件

1. 11.1 的调用点 file:line 回填完成；
2. 11.2 的 docs-uml 同步条目被接受；
3. 11.5 的 `open(WindowState)` 形态写入接口草案（§7 已按此更新）。

## 12. 已核实阶段表（代码为准，取代 §3 的假设）

核实结论先行：**43499 不支持 `pre_spawn` 与 `post_spawn`**，只有终端接管前的一个可用插入点；43284 支持 `post_terminal`（唯一），其余三阶段不可用。

### 12.1 43499（`cve_2026_43499`）

| stage | 触发点（file:line） | 前置条件（窗口/能力） | 允许能力 | 结论 |
|---|---|---|---|---|
| pre_spawn | **无可插入点**。race 窗口在**每次写尝试**内开关：`steps.cpp:118` `Cve43499Primitives::attack_write<M>(...)`（W2/W3 的写循环内），`userspace_clean` 由 route 实现在**同一次调用内**置位（`route/multicast_waiter_route.cpp:53/71/86/133`、`select_stack_route.cpp:125`、`tcp_zerocopy_route.cpp:95`） | W1/W2/W3 之间不存在「窗口已关闭且 child 未建立」的点：窗口随每次 `attack_write` 开关，而 victim/child 就在该写循环里建立（`steps.cpp:480` w2 内的 victim round） | — | **不可用**（原因：窗口非单一区间，且「已关闭 + child 未建立」的点不存在） |
| post_spawn | 同上：child 建立发生在 `steps.cpp:480` `w2<M>()` 内部（写循环里），此时**窗口仍会为后续写尝试重新打开** | child_task 有效，但窗口未终结 | — | **不可用**（原因：后续写尝试会再次进入 race 窗口，违反 R1） |
| pre_terminal | `steps.cpp:484-490`（W1W3：w3 之后、`return StageResult::Continue` 之前）/ `steps.cpp:517-521`（W1W2 同形）；随后 `cve_2026_43499_backend.cpp:136` 检查结果 → `:138-158` 移交 rooted child | **窗口已关闭**（最后一次 `attack_write` 已返回，无后续写）**且**已具备内核写能力（W1 已 permissive；`w1<M>` 在 `steps.cpp:386`，SELinux + scratch repair） | kernel_read / kernel_write / alias / child_task（child 已建立） | **可用（唯一）** |
| post_terminal | — | 43499 终端接管后宿主无法回传（root child 已独立执行） | — | **不可用**（已按先例文档化） |

**`pre_spawn` / `post_spawn` 边界建议（请裁决）**：43499 上两者**合并为 `pre_terminal` 一个语义**——即「最后一次写尝试结束、终端接管之前」。理由：race 窗口按写尝试开关，(a) 窗口关闭与 (b) child 建立两件事在时间上交错，无法同时满足「窗口关闭 + child 未建立」；强行拆分只会得到两个都无法安全加载插件的点。能力差异也不再存在（child_task 在该点已有效）。

### 12.2 43284（`cve_2026_43284`）

| stage | 触发点（file:line） | 前置条件（窗口/能力） | 允许能力 | 结论 |
|---|---|---|---|---|
| pre_spawn | 无对应概念：43284 没有 waiter race，其链是 pagecache 写入 + UMH 触发的 LKM 加载（`execution_binding.cpp:103-232` 绑定 → `backend_terminal.cpp:201-221` 组链请求） | 无 waiter 窗口；但绑定期的 carrier/hook 尚未进入终端接管 | — | **不适用**（backend 无「spawn」概念；不建议为凑齐四阶段硬插） |
| post_spawn | 同上：43284 的 root 进程由 UMH/LKM 侧拉起（`terminal/umh_forward` + LKM UMH 脚本），宿主侧没有「child_task 有效」的时刻 | — | — | **不适用** |
| pre_terminal | `backend_terminal.cpp:201-217`（构造 `ChainRequest` → `run_chain`）之前 | LKM 尚未驻留、内核能力尚不可用 | kernel_read/write/alias（若已 permissive） | **可用（可选）**，但价值低（与 43499 的差别是此时无内核特权） |
| post_terminal | **`lkm_window.cpp:99-107`**：注释明示「POST_TERMINAL is the one stage inside the LKM residency window」，`registry_->dispatch(..., CountermeasureStage::PostTerminal, &host_ops_)` | **LKM 已驻留**（`/dev/glk` 可用）→ KernelMemory/KernelAlias 有效 | kernel_read / kernel_write / alias（**唯一同时具备内核能力的窗口**） | **可用（唯一，且为插件的主场景）** |

### 12.3 汇总（供 docs-uml 同步）

- 43499：`pre_spawn`/`post_spawn`/`post_terminal` 三阶段**不可用**；仅 `pre_terminal` 可用（`steps.cpp:484-490` / `:517-521`）。
- 43284：仅 `post_terminal` 可用（`lkm_window.cpp:99-107`）；`pre_spawn`/`post_spawn` **不适用**；`pre_terminal` 可选但无内核特权。
- 需同步到 `contract-design.md` §3.14.7 与 `full-process-uml.md`（§1 IPO / §2 状态机）：上述「不可用/不适用」必须**显式写出**，不得沉默缺席；43284 的 `post_terminal` 画在 LKM 驻留窗口内；43499 状态机只画 `pre_terminal` 一处插件派发。
- 由此对 §9 落地顺序的影响：step 3 接线**先只接 43499 的 `pre_terminal` 与 43284 的 `post_terminal`**（两个真实可用的点），其余阶段在注册期即拒绝（`plugin/wire.cpp` 只接受四个 stage token，但 host 在 `open()` 时按 backend 校验可用集合——该规则写入 step 2 的 host 接口）。
