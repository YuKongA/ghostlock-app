# P2 设计规格：新契约层（兼容面 + 新架构面）

> 分析类（≤8 KiB）。**自包含**。P2 唯一权威规格：目标 = **既有 `backend/**` 零改动可编译**（[rewrite-plan](../plan/rewrite-plan.md) §5 硬判据）。
> 全量表 = [A1](contract-layer-p2-spec-annex-a1.md)（48 头 × 次数 × 职责）+ [A2](contract-layer-p2-spec-annex-a2.md)（符号使用点）。判据 = D1。

## §1 兼容契约（**实测**：48 个头 / 59 个 backend 文件）

- **现状**：上层全禁用（`*.disabled` = **194**；上层残留 `.cpp/.hpp/.h` = **0**）；`backend/**` **88 保留、零改动**；`src/lib` **4** 保留。
- **依赖面（实测）**：`backend/**` 的 `#include` 指向已禁用上层头的 —— **直接依赖头 48 个**（涉及 59 个 backend 文件）；再加 **10 个 include 闭包的传递头** ⇒ **交付头集合 = 58**（10 头清单见 [A3](contract-layer-p2-spec-annex-a3.md)；U-1 已结清）。
- **兼容契约**：新契约层**沿用既有头路径名**，为每个头提供 **最小符号集**（backend 实际使用的类型/常量/函数/宏）⇒ 59 个文件**逐字节零改动**可编译。
- **最小符号集 = 使用点驱动**（逐头「符号 + 使用点」表见 [A2](contract-layer-p2-spec-annex-a2.md)）：`CoreSession`（`cve_2026_43284_backend.hpp:13/:41`）· `TargetProfile`（`cve_2026_43499_state.hpp:26`）· `race::PiRace`（`:29`；route 入参 `select_stack_route.cpp:12`）· `profile::Document`（`cve_2026_43284_backend.hpp:52`）· `contract::StageResult`（`:14/:56`）· 中性支持面符号（`read_first_line`/`log_sync`/`fail_stop_dirty_race`…）。
- **48 行全表**（含「仅传递包含」的头）见 annex；**必须逐一覆盖**，缺一即 59 文件之一编译失败。

## §2 冻结量（**不得漂移**，全部为编译期 `static_assert` 或常量）

| 量 | 值 | 出处 | 约束 |
|---|---|---|---|
| `CoreSession` 槽 | **2048 B**（`kBackendStateBytes`） | `session/core_session.hpp.disabled:18`（槽 `:23`，`alignas`） | 槽**紧邻 `runtime`** ⇒ 位置/对齐/尺寸不变 |
| `kernel_offsets` | **520 B** | `model.hpp.disabled:425`（`static_assert`） | 布局守卫：改动即编译失败 |
| `TargetProfile` | **784 B** | 同上 `:427`（`static_assert`） | 同上（NDK aarch64；注释 `:424` 记 520/8、784/8） |
| **C ABI 头** | `contract/abi/glk_contract_abi.h` | 1 个 backend 文件包含 | **可被 C 编译**：门禁 = `cc -x c -fsyntax-only <header>` |

## §3 十处关键依赖：**为何不可最小化**

| # | 头 | 为什么不能只给「同名空壳/最小声明」 |
|---|---|---|
| 1 | `session/core_session.hpp` | **2048 B 槽 + 对齐 + `state_construct/state_destroy` 契约**（`cve_2026_43284_backend.hpp:41`）⇒ 槽尺寸/位置即 ABI |
| 2 | `contract/model.hpp` | 承载 **520/784 `static_assert`** 与运行期参数结构 ⇒ **冻结量载体** |
| 3 | `memory/target.h` | `TargetProfile` 为 **43499 state 成员**（`cve_2026_43499_state.hpp:26`）⇒ 需完整定义（含布局） |
| 4 | `memory/payload_builder.h` | 写请求**不可变描述 + `preserve_child` 布局选择** ⇒ 语义决定内核写行为 |
| 5 | `profile/document.hpp` | `profile::Document` 为 backend bind **入参**（`cve_2026_43284_backend.hpp:52`）⇒ 需完整类型与取值接口 |
| 6 | `profile/schema.hpp` | `BindCode`/`FieldSpec` 决定**具名拒绝语义**（`missing_required`…） |
| 7 | `race/pi_race.h` | `race::PiRace` 为 state 成员（`:29`）+ route 入参（`select_stack_route.cpp:12`） |
| 8 | `contract/identity.hpp` | 组件 id / 可用性谓词**唯一权威**（ADR-0004）⇒ 新面须一致，**不得重定义第二份** |
| 9 | `capabilities.hpp` + `stage_result.hpp` | 参与控制流判定（后端返回值 `:56`）⇒ 类型与枚举值必须同义 |
| 10 | `support/decls.hpp` + `log.hpp` + `native_resource.hpp` | 中性支持面（7–11 处使用）；**符号名与签名即契约** |

## §4 新架构面与兼容面的关系（**不得互相替代或重复定义**）

| 面 | 内容 | 与兼容面的关系 |
|---|---|---|
| **兼容面** | §1 的 48 个同名头 + 最小符号集；§2 冻结量 | **只提供既有名字与既有语义**；**不新增**能力 |
| **中性写面** | op token / `WriteOutcome` / 诊断 | 新名（`contract` 新面）；与兼容面的 `payload_builder` 等**单向依赖**：新面可 include 兼容面，**反之禁止** |
| **OpRegistry** | 脚本唯一接口权威（R2） | 独立于兼容面；**不得**为兼容而复制 OpSpec 定义 |
| **plan gate** | 十条例（`{ok,reason,path,index}`） | 复用兼容面的**具名拒绝风格**（`BindCode` 同义），但**不重复定义**其枚举 |
| **句柄表** | `{kind,index,generation}`、有界 64、POD、**无 `__gc`**（[ADR-0010](adr/0010-lua-cpp-resource-model.md)） | 与兼容面无交集 |
| **窗口宿主** | 窗口内零 Lua / 参数化 / 原子性（[pi-window-constraints](pi-window-constraints.md)） | 依赖兼容面的 state 类型（`PiRace`/`TargetProfile`），但**不得**把窗口逻辑写进兼容头 |

- **判据（防重复定义）**：任何「同一事实两处定义」（如 `TargetProfile` 在兼容头与新面各写一份）**视为**违反单一权威（E5/AGENTS M5 教训）。

## §5 分层与 R1 层表登记

- 新契约层 = **唯一允许被 `backend/**` include 的上层**；其余新层（会话/装配/脚本）**不得**被 backend include。
- **R1 防火墙**：层表由 host 测试强制；**白名单 0 条**（空账本 + pin）；新增层名须**同批**登记。
- 兼容头**按既有层名归位** ⇒ 层边与禁用前**等价**（重写不得顺带放宽层约束）。

## §6 判据（**必须能失败**）

- **白盒（硬判据）**：**交付头集合 == 58**（48 直接 + 10 传递，[A3](contract-layer-p2-spec-annex-a3.md)；少一即某 backend 文件编译失败）· **59 个 backend 文件零改动**编译通过（逐文件 0 error）· `git status` 证明 `core/src/backend/**` 零修改。
- **白盒（C ABI）**：`cc -x c -fsyntax-only core/src/contract/abi/glk_contract_abi.h` ⇒ **退出 0**（证明可被 C 编译）。
- **白盒（冻结量）**：`static_assert` 三条（2048 槽字节数、520、784）保留且可编译。
- **黑盒**：同一 backend 文件编译产物**链接通过**（无缺失/无重复符号）—— 观测点 = 链接器错误为空。
- **必须能失败（证伪）**：删任一最小符号（或 2048→1024）⇒ 指定 backend 文件**必须编译失败**；实验后还原并**复核**（`rm -f` 产物 + 重建）。
- **极端值**：空符号集 · 两个同名头共存（应失败）· C ABI 头塞 C++ 构造（应失败）· 槽尺寸 ±1 B（应失败）。

## §7 实现顺序与反工时点

| 步 | 内容 | 反工时点（晚做会返工） |
|---|---|---|
| 1 | **固化依赖面**：48 头 × 59 文件 × 最小符号集（annex 表即冻结输入） | 未固化就动手 ⇒ 每加一个头都要回头补符号 |
| 2 | **冻结量常量**（2048/520/784 + C ABI 头） | 晚定 ⇒ 兼容头与冻结量互相改（双层返工） |
| 3 | **48 个同名头骨架**（include 守卫 + 命名空间 + 占位声明） | 晚做 ⇒ 59 文件一直不能编译，无法增量验证 |
| 4 | **逐头填最小符号集**（以使用点为准） | 先写新面再补兼容 ⇒ backend 反复改（最贵返工） |
| 5 | **零改动验证**（§6 白盒/黑盒 + 证伪实验） | 判据不留证伪 ⇒ 守卫可能 vacuous（AGENTS 已记载一次真实教训） |

## §8 未决

| # | 未决项 | 处置 |
|---|---|---|
| U-1 | ~~47 vs 48~~ **已结清：48 为准**（47 少算 1） | 去重 102 ⇒ 非 backend/lib **52** − 4 个 `leak/` 自有头 = **48**；命令见 [A2](contract-layer-p2-spec-annex-a2.md) §A3 |
| U-2 | 「仅传递包含」的头是否仍需提供 | **是**（include 即契约，可为空体） |
| U-3 | `plugin/**`、`terminal/**` 头是否随 P5 重写 | P2 **先按兼容面提供**；重写归 P5 |
| U-4 | annex 是否纳入 lint | 纳入（`docs/**` 已在 globs 内） |
