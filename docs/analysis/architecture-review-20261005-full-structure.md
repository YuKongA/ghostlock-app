# 架构审查（完整结构）：逐层类型/函数 + 设计指南与哲学文档

- 日期：2026-10-05　审查人：Lead　快照：工作树（R6b/T5 半落地 + ADR-0006 T1–T4 已实现，均未提交）
- 方法：对照 `design-philosophy.md`（11 条）、`engineering-standards.md`（§2 架构 / §3 数据流 / §5 数据结构）、
  ADR-0001（允许依赖图）/ADR-0004（组合轴）/ADR-0006（terminal 归属），以及**声明结构** `src/core/README.md`；
  逐层核对「**声明职责 vs 实际代码**」，并对每个主要类型/函数给出 功能·作用·影响·兼容性。
- 本轮增量审查（F1–F10）见 `architecture-review-20261005-r6b-term.md`；本文件为**全量结构审查**并吸收其结论。

## 1. 声明结构 vs 实际（逐层）

| 层 | 规模 | 声明职责（README/规范） | 实际与偏差 |
|---|---|---|---|
| `contract` | 11 文件 / 1871 行 | 中性词汇与契约：identity、capability、ABI 映射 | **职责在扩张**：词汇 + 组合表（新 `CombinationKind/Spec/Catalog`）+ 能力接口 + C ABI 映射同处一层（G4） |
| `pipeline` | 3 文件 / 388 行 | 只做组合/分派；稀疏 triple | 已收敛为 `(backend, token)`（T5）；`dispatch_target_of` 出现 **3 个重载**（旧语义是否彻底移除待确认，F-旧） |
| `profile` | 8 文件 / 1774 行 | framing + 绑定；不解释字段 | R4 后 `document.hpp` 新增 `is_text/text` 且 `ReadResult` **持有缓冲**（防悬垂）✓；`schema.hpp`/`registry.hpp` 承载默认值与必需性（R1）✓ |
| `plugin` | 12 文件 / 2519 行 | 对策插件设施（loader/registry/controller + C ABI） | 与 `contract/abi` 双投影一致 ✓；插件参数 schema 与 extractor 投影**未实现**（P1/P2 待做） |
| `platform` | 5 文件 / 974 行 | abi/runtime/device_facts/vivo | `vivo` 仍在本层（ADR-0006/R4c 计划插件化）；`device_facts` 已支持降级（app 域可用）✓ |
| `terminal` | 11 文件 / 865 行 | 中性终端与输入载荷 | **TERM 后**：`root_child.cpp` 已下沉 backend，但 `root_child.hpp`（声明）仍在此层 → **声明/实现分居**（G7/F5）；README 仍写 `terminal/root_child.*`（G1） |
| `backend` | 10 顶层文件 + 各 CVE 子目录 | 按 CVE 的 backend；占位头 | 4 个占位 backend 头与真实 backend **混排**在顶层（G6）；43284 新增 `entry.{hpp,cpp}` 缝合（入口 include 4→1）✓ |
| `session`/`memory`/`race`/`support` | 183/600/406/2086 行 | 状态容器/地址与载荷/竞争/通用件 | **R1 搬迁已完成**：`support/util.cpp` **799 → 133 行**（12 个中性函数），14 个攻击相关件迁入 `backend::cve_2026_43499::spray`（`spray.{hpp,cpp}`），**防火墙账本归零**（`176 files, 0/0/0/0`）；`kernelsnitch.h` 的唯一 TU 现为 `spray.cpp` |

## 2. 主要类型与函数（功能 / 作用 / 影响 / 兼容性）

### 2.1 选择与组合（本轮核心）

| 符号 | 功能 | 作用（消费者） | 影响（改动波及） | 兼容性 |
|---|---|---|---|---|
| `contract::CombinationKind`（13 值） | 组合紧凑 id | catalog / 诊断 | 加 backend/route/path → **线性膨胀**（F1） | 内部，无 ABI 承诺 |
| `contract::CombinationSpec` | token → {backend,route,steps,terminal,available} | 解析期、门禁、UI（应对拍） | **10 B padding 卡 lint**（F2） | 表结构可变；wire 不受影响 |
| `kCombinationCatalog[12]` | 白名单（6 可用 + 6 计划） | 未知 token 拒绝依据 | 增删 token 需同步 Kotlin（F4） | 与设计矩阵一致 ✓ |
| `combination_resolve/name/available/stepset_wire` | 解析/命名/可用性/旧 id 映射 | 绑定与分派 | 旧 uint（1/2/3）→ token 在此 | 旧 uint 兼容 ✓ |
| `pipeline::dispatch_target_of` / `path_target_of` / `Pipeline::target` | 组合 → 编译期 target | 每个组合 static_assert 锁定 | 新增组合必须登记 | ✓ 编译期优先（哲学 4） |
| `profile::Document::Value::is_text/text`、`ReadResult::storage` | string 值 + 所有权 | 43284 路径字符串 | 绑定期不得悬垂（已用 unique_ptr 持有）✓ | wire `str` ≤256 B、canonical 不变 ✓ |

### 2.2 终端与入口（ADR-0006）

| 符号 | 功能 | 作用 | 影响 | 兼容性 |
|---|---|---|---|---|
| `terminal::write_root_script(bool safe_mode)` | 脚本文本生成（去 backend 依赖） | 43499 handoff | 文本**逐字节不变**（sha256 证据）✓ | 零行为变化 |
| `backend::cve_2026_43499::terminal::root_child.cpp` | 43499 落地实现 | root_child 路径 | Makefile、防火墙 170→173 | 命名空间未搬（F5） |
| `backend::cve_2026_43284::entry::{run_diagnostic,run_staged,ProductionSession}` | 入口缝（侧信道/绑定/钩子） | `main.cpp` | 直连 backend include 4→1；**无新增堆分配**（保护 spray）✓ | CLI/日志/退出码不变 ✓ |
| `terminal::UmhForwardPolicy` | 中性转发（注入 channel） | 43284 umh 路径 | 保持中性 ✓ | — |

### 2.3 后端策略（R4/R6a）

| 符号 | 功能 | 影响 | 兼容性 |
|---|---|---|---|
| `backend.cve_2026_43284.{carrier_path,lkm_path,defex_symbol}` | uint token → **string 路径** | manifest 类型、金标、UI 校验 | 旧 token 语义等价（默认解析不变）✓ |
| `backend.cve_2026_43284.{wait_timeout_ms,module_poll_attempts,module_poll_interval_ms}` | 硬编码 → 文档字段 | 运行时长可配 | 默认值 = 旧常量 ✓ |
| `common.fallback_route` | **已删除**（R6a） | wire/manifest/金标/UI | 旧导出 bin 被 fail-closed 拒绝（须用当批文档） |

## 3. 设计指南与哲学文档审查（条文是否仍成立）

| # | 文档/条文 | 现状 | 判定 | 动作 |
|---|---|---|---|---|
| G1 | `src/core/README.md`：`terminal/root_child.*` 在 terminal 层 | TERM 已把实现移到 backend | **过时** | 更新为「声明在 terminal（过渡）/实现在 backend」，并写明 T5 收尾 |
| G2 | `src/core/README.md`：`pipeline` 稀疏 triple 描述 | T5 已收敛 `(backend, token)` | **过时** | 同步；同时记录 `CombinationSpec` 权威在 `contract` |
| G3 | `AGENTS.md`：「防火墙 165 files」「v2 owner Schema manifest」等 | 实际 173；v2 manifest 已删 | **过时**（部分已由我修） | 补一次全文核对（含 route 并入 token 的描述） |
| G4 | `engineering-standards.md` §2.0「权威 = ADR-0001/0002」、§2.2「route 结构」、§3.1「配置单一权威」 | 已有 ADR-0004/0006、token 单一权威、S4 批次 | **落后于已批准决策** | 更新权威链与「选择只来自 wire/token」的规则 |
| G5 | `design-philosophy.md` 原则 3/10（单一权威、机制防错） | 跨语言 token 表**未导出**（F4） | 条文成立，**执行缺口** | 在 §3.1 增补「白名单/词汇须导出并对拍」的操作条目 |
| G6 | `design-philosophy.md` 原则 5（未列入清单即冻结） | 本轮双流并发改同一树 → 中间态不可编译 | 条文成立，**执行缺口** | 增补操作条目：**同一时刻只允许一条写入流**；批次原子提交 |
| G7 | 缺决策记录：`RouteKind::Auto` 的退役语义；`contract` 内部再分层 | — | **缺失** | F3 落 `RouteKind::None`；G4（contract 再分层）排入 R5 |

## 4. 发现汇总（本文件新增 G，并吸收 F）

| # | 级别 | 结论 |
|---|---|---|
| **F4** | **高** | Kotlin 侧**没有** token 表 → 跨语言单一权威未闭合；UI 若硬编码必然漂移。**落地前必须先补设计**（导出 + agreement test） |
| F1 | 中 | `CombinationKind` 是 backend×route×path 扁平乘积 → 扩展性隐患；建议分解为 `{backend, RouteKind\|None, PathKind}` |
| F3 | 中 | `RouteKind::Auto` 被复用为「无 route 轴」哨兵 → 一符两义；应引入 `None`/`optional` |
| F2 | 中 | `CombinationSpec` padding 10 B → **lint EXIT=2（红线）** |
| F5/G7 | 中 | TERM T2 声明/实现分居（`terminal/root_child.hpp`）→ 应随 T5 收尾 |
| F7/G6 | 中-高 | 双流并发 → 中间态不可编译；须原子提交 + 单写入流 |
| G4 | 中 | `contract` 层职责扩张（词汇 + 组合表 + 能力 + ABI，1871 行/11 文件）→ 内部再分层 |
| G1/G2/G3 | 中 | `src/core/README.md` 与 `AGENTS.md` 存在**过时描述**（terminal 位置、triple→token、防火墙计数） |
| G5 | 中 | `engineering-standards.md` 权威链与规则落后于 ADR-0004/0006 |
| G6b | 低 | `backend/` 顶层占位头与真实 backend 混排 → 建议归置或由 catalog 的 `available=false` 表达 |
| F6 | 低 | 「选择词汇」住在容器层（`contract` → `profile::RouteKind`） |
| F8 | 低 | `StepSetKind` 已是派生量但可直接选择 → 标为内部 |

## 5. 结论（思想先行）

**结构合理性判定**：本轮方向（token 单一权威、编译期锁定、计划项显式、terminal 实现下沉、入口缝合）**与哲学一致**，
但存在 **1 个必须先补的设计缺口（F4）**、**2 个结构语义问题（F1/F3）**、**1 个门禁红线（F2）**、**1 个最难维护的过渡态（F5）**、
**1 个过程违规（F7）**，以及**文档滞后（G1–G5）**。

**建议顺序（不落实现先落思想）**：
1. **设计补丁**：R6b 设计 v3 —— F1 维度分解、F3 `None` 语义、**F4 token 表导出格式**（并入 manifest 或独立 `--export-combinations`）+ 计划项如何呈现给 UI；
2. **规范同步**：G4/G5/G6 的条文增补（权威链、导出对拍、单写入流）；
3. **结构修复**：F2 字段序、F3 落地、F5 收尾；
4. **原子提交**：R6b/T5 一次提交，TERM 单独提交（F7）；
5. **门禁**：host/NDK/lint/Kotlin/cargo + 真机 `mcast_rootchild`/`pselect_rootchild`/`tcp_rootchild` 各一次 + 43284 `umh` + AVB；
6. **文档收口**：G1/G2/G3（README/AGENTS 同步）。
