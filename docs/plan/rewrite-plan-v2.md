# 重写计划 v2（src/core 全量重写 · 2026-10-08 前提变更）

> 计划类（≤16 KiB）。**自包含**。本文 = **现行唯一重写计划**；v1（[rewrite-plan.md](rewrite-plan.md)）与 P2/P4/P5 各规格件**作废但原文保留**（沿革不删，见 §0）。
> 判据形式 = D1（黑盒/白盒 + 极端值）；权威顺序 = 守则 > AGENTS > engineering-standards/design-philosophy > analysis 文档。

## §0 前提与沿革

- **前提变更（用户 2026-10-08 裁定）**：**完全重写** —— `src/core` **已清空**（提交 **`b0e9f499`**），新实现**从零**建立；**旧路线整体作废**。
- **旧路线（沿革，不删原文）**：P0 规划 → P1 禁用上层（197 → 194 个 `.disabled`）→ P2 兼容层（58 同名头 · `compat-check` 97/0）→ P4 里程碑 A/B（`attack_window` / `composition_root` / `resource_scope` / `plan_gate`）。**该路线的产物已不存在于工作树**，其文档作为**输入规格与历史证据**保留：
  [rewrite-plan.md](rewrite-plan.md)（v1）· [contract-layer-p2-spec.md](../analysis/contract-layer-p2-spec.md) 及 [A1](../analysis/contract-layer-p2-spec-annex-a1.md)/[A2](../analysis/contract-layer-p2-spec-annex-a2.md)/[A3](../analysis/contract-layer-p2-spec-annex-a3.md) · [p2-compat-gate-20261008.md](../analysis/p2-compat-gate-20261008.md) · [p2-review-20261008.md](../analysis/p2-review-20261008.md)（+[附件](../analysis/p2-review-20261008-annex.md)）· [p2-minimisation-plan.md](../analysis/p2-minimisation-plan.md) · [composition-layer-p4-spec.md](../analysis/composition-layer-p4-spec.md)（+[附件](../analysis/composition-layer-p4-spec-annex.md)）· [handoff-script-layer-p5-spec.md](../analysis/handoff-script-layer-p5-spec.md)。
- **旧实现可取回位置（**只读取回**）**：
  - **tag `legacy/baseline-20261008`**（基线快照，**首选**）；
  - 提交 **`ec3bb923`**（「roll the code back to the planning baseline」）与 **`b0e9f499^`**（清空前的完整树）；
  - 取回形式：`git show <rev>:<path>`（**只读**）或 `git worktree add <tmp> <rev>`（隔离检出）；**禁止** `git checkout/restore/stash/reset/clean` 及等效写回（AGENTS 硬规则）。

## §1 现状（实测，2026-10-08）

| 项 | 现状 |
|---|---|
| `src/core` | **0 文件**（整树清空；`.disabled` 参考件亦已不在工作树） |
| 构建引用 | **悬空**：`src/Makefile` **621 处** `core/` 引用 · `src/CMakeLists.txt` **24 处** |
| Kotlin / 资产 | **未动**（`app/**`、`profile-core/**` 工作树 clean；profile/资产与 UI 面完整） |
| 设计族 | **完整**：`docs/analysis/**` 62 篇 + `docs/plan/**` 4 篇（含 ADR-0001…0010、窗口约束、三端打通、R 系列规格） |
| 攻击面 | `backend/**`、`race/**`、`terminal/**` 等**均不存在** ⇒ **任何「沿用旧结构」的假设作废** |

- **结论**：本次不是「改」而是「**重建**」——构建入口必须重写，结构必须新定；**可复用的只有数据（常量/几何/时序）与设计结论**（§3）。

## §2 从零序列 A0–A6（单变量，逐阶段判据全绿才前进）

| 阶段 | 交付 | 判据（**可证伪**） | 门禁 |
|---|---|---|---|
| **A0 骨架与构建入口** | `src/core/` 目录骨架 + 最小入口（`main.cpp`）+ **`Makefile` / `CMakeLists.txt` 同批重写为新树**（不再引用旧 `core/` 路径）+ **含 P4 五文件按 git 恢复**（`contract/resource_scope.hpp` · `pipeline/attack_window.hpp` · `pipeline/composition_root.hpp` · `contract/op_registry.hpp` · `pipeline/plan_gate.hpp`；来源 = **`63a87af8`（A）+ `9b46d5b5`（B）**，两个测试件按需一并恢复） | host 构建 **EXIT=0**；**新子树编译 0 警告**；`core/` 悬空引用归零 | G1 |
| **A1 中性契约层** | `contract/`：identity/能力概念 + op token / `ParamSpec` / **句柄模型（[ADR-0010](../analysis/adr/0010-lua-cpp-resource-model.md)：`{kind,index,generation}`、有界 64、POD、无 `__gc`）** | 头可 **host 编译**；概念 `static_assert` **可失败**（证伪实验）；无第二处定义（E5） | G1 |
| **A2 支持 / 内存 / 竞争** | `support/`（日志/时间/中性面）· `memory/`（`TargetProfile` 与几何常量）· `race/`（PI 竞争原语） | 各层**模块单测**；**窗口约束**接线（[pi-window-constraints](../analysis/pi-window-constraints.md) 白名单/禁项 + `nm` 无 `_ZTV*` + 逐退出路径回收） | G1 |
| **A3 43499 攻击链** | `backend/cve_2026_43499/**`：W1–W3 步骤 + route 策略（`select_stack`/`tcp_zerocopy`/`multicast_waiter`） | 步骤级单测 + **窗口内零 Lua** + 参数化上界；**首次触真机** | **G2** |
| **A4 43284** | `backend/cve_2026_43284/**`（pagecache/UMH）+ **执行器四选一**（`UserRoot`/`Umh`/`CredGrant`/`KernelCmdSet`，构建期决定） | 执行器可用性判据 `executor-unavailable` 可测；pagecache 路径单测 | G1（+G2 按触达面） |
| **A5 terminal / handoff** | terminal 词汇与实现 + handoff 组装（模板 + exec 列表 + `always` + **三层收尾**：模板后段 → always → 内核兜底） | 收尾三层各自可测；产物字节 + sha256 可复现 | G1（+G2 按触达面） |
| **A6 Lua（脚本计划）** | 沙箱 + 计划装载 + 内置计划脚本（**计划 = 脚本**；queue 已退役 `queue-removed`；缺件 `plan-missing`） | 沙箱**符号级证据**（无 `io/os/package/debug/load/dofile/require`）+ 两条证伪（放开 `load`/`dofile` ⇒ 指定测试失败；去代际 ⇒ 旧句柄用例失败） | G1（+G2 若触写路径） |

- **A6 前置**：**R10（Lua 配置结构）裁定**未定 ⇒ 数据形态（默认值/必填范围）随裁定；未裁定前 A6 只做沙箱与句柄。
- **纪律**：每阶段**只做一类事**；上一阶段判据未全绿不得进下一阶段（守则 §585）；写入恒为**单流**。

## §3 数值与结构规则（**用户裁定 2026-10-08**）

- **最小原理（只留这一条）**：**数值不入 C++** —— 一切可调数值（时序、次数、超时、重试、阈值、几何调参）**一律从 profile / 资产读**；**缺失即上报缺口，不得硬编码**（缺什么报什么，补齐后再跑）。
- **机制常量例外**：仅当某数值是**协议/结构本身**（magic、结构尺寸、布局偏移、字段宽度、线格式语义）才允许写进 C++，且**必须注释说明**「为何属机制而非调参」并给出处。
- **结构零移植**：目录/模块结构 · 类与函数划分 · include 关系 · 构建清单组织 · 命名层级 **一律不移植**；不得以「与旧实现同名同形」为由照搬（结构相似度异常 ⇒ 举证+登记）。
- **禁止**：把旧树整体复制回来当新实现；任何复制须**逐文件举证并登记**。
- **旧值的使用方式**：旧实现与旧 profile 里的数值只作**对照与取值来源**（`legacy/baseline-20261008` 只读取回）；**取值须落到 profile/资产**，而不是落进 `.cpp`。

## §4 判据与门禁

- **通用判据**：新子树 **编译 0 警告**；每层**可证伪**（D1：黑盒/白盒 + 极端值；守卫必须能失败，实验后 `rm -f` 产物重建）；**无第二读取路径**（E5）。
- **里程碑**：**E2E-1 = host 全流程**（[host-assembled-integration-plan](../analysis/host-assembled-integration-plan.md)：extractor → Kotlin → 帧 → native → dispatch；**输出固定打印「非真机判据」**）；**G2 = 真机门禁**（冷机 / 固定 CPU 对 / 单 route / KernelSU 未加载；归档 `docs/analysis/device-gates/*.md`）。
- **旧编号处置**：`TD-REWRITE-1/2/3`（v1 的越权/技术债登记）**随旧路线作废**（其对象 = 旧树与 `.disabled`，已不存在）⇒ 归档入 v2 沿革；**若新路线再出现越权**，另立 **`TD-RW2-*`** 编号并同样登记代价/偿还时机/owner。
- **门禁清单（本计划适用）**：G1 = host 构建 + 模块单测 + `markdownlint`；G2 = 真机门禁；E2E-1 为 host 里程碑。

## §4.1 已知缺口（开工前必须知道）

| # | 缺口 | 影响 | 处置 |
|---|---|---|---|
| **GAP-2** | **旧实现硬编码调参（完整 11 条，考古定稿 2026-10-08）** | 若照抄进新树即违反 §3（数值须走 profile） | **11 条**：① `steps.cpp:95` 退避 100ms ② `:103` 校验后 50ms ③ `:118` park-retry 50ms ④ `:247/:258` W3 重试 100ms ⑤ `multicast_waiter_route.cpp:36` `kMulticastArmHold=20000` ⑥ `memory/constants.hpp:43` `PSELECT_CONSUMER_SETTLE_USEC=250000` ⑦ `select_stack_route.cpp:75` `usleep(1000)` ⑧ `primitives.cpp:58` `usleep(20000)` ⑨ `:68` `sample_period=5000` ⑩ `race/threads.cpp:45` 20ms ⑪ `:141-143` nice 轮转 `(calls%19)+1` ⑫ `spray.cpp:548` 泄漏轮询 50ms —— **处置 = 全部改为 profile 参数**（缺参数 ⇒ 具名拒绝，不兜底）；**正例对照**：`steps.cpp:62-77/:101/:166` 的 `attempts`/`settle_usec` 与 `tcp_zerocopy_route.cpp:277` 的 `profile.tcp_attempts()` 已参数化 |

## §4.3 已关闭缺口（沿革 · **已不再阻塞**）

| # | 缺口 | 关闭事实 | 关闭日期 |
|---|---|---|---|
| **GAP-1** ~~`execution-multicast-waiter.conf` 缺失~~ | 原影响 = multicast 在补齐前不可运行 | **资产已在** ✓：`app/src/main/assets/profile/execution-multicast-waiter.conf`（**1606 B**）· 加入提交 **`7b34bb44`**（`feat(profile): give the multicast preset a reader…`） | **2026-10-08** |

- **影响面更新**：`docs/analysis/a3-tuning-migration.md` §5 中「`routes.multicast_waiter.*` 依赖 GAP-1 先补齐」的前提**已消失**（该键可随 A3 批次直接落）⇒ 评审「先落 multicast」的排序不再受此缺口阻挡 ✓。

## §4.2 参数归属（考古定稿 · 30 个访问器 / 机制 / 运行期量）

- **驱动器参数（attempts / settle_us / verify / 退避）不进 op，属 profile 阶段策略** —— 旧实现已有 **30 个访问器**：`w1_attempts` · `w2_attempts` · `w3_attempts` · `w1_settle_us` · `w2_settle_us` · `w3_settle_us` · `w1_scratch_repair_attempts` · `w3_chain_rounds` · `heap_prepare_max_attempts` · `heap_prepare_timeout_ms` · `heap_kernelsnitch_timeout_ms` · `race_state_poll_interval_us` · `race_setup_settle_us` · `race_route_wait_ms` · `race_route_done_timeout_ms` · `select_*`（select 族） · `tcp_*`（tcp 族） · `multicast_*`（multicast 族） · `handoff_*`（handoff 族）。
- **route 几何**：`tcp_*` / `select_*` / `multicast_*` 各自的**布局与调参**属 **profile 数据面**（不进 C++）。
- **机制常量（留 C++）**：`WriteMode` 枚举 · waiter 结构 `0x30` · 页 `4096` · direct-map 守卫 · futex 码 · 字段偏移（结构布局本身）。
- **运行期量（不可配置）**：目标绝对地址 · 喷溅命中页 · 泄漏 task · waiter 地址 · pid —— 只能来自运行期观测，**不得**进 profile。

## §5 Q1–Q4 裁定（**已裁定 2026-10-08**）

| # | 裁定 | 落地要点 |
|---|---|---|
| **Q1** | **按 git 恢复** P4 五文件（`63a87af8` + `9b46d5b5`）作 A0 起点 | 恢复后**按本 v2 规则复核**（结构可留、数值改走 profile）；两测试件按需恢复 |
| **Q2** | **数值不入 C++**（比"移植数值"更严） | 见 §3：数值一律从 profile/资产读；**缺失即上报缺口**；仅**机制常量**可入 C++ 且须注释说明 |
| **Q3** | **构建入口同批重写**（`Makefile` 621 处 + `CMakeLists.txt` 24 处） | 归 **A0**；A0 判据含「悬空 `core/` 引用归零」 |
| **Q4** | **PENDING-DECISIONS 逐条复核** | 已按「仍适用 / 作废 / 待重述」逐条标注（见 [PENDING-DECISIONS](PENDING-DECISIONS.md) §A–§F 各行状态） |

- **说明**：四条均**不阻塞 A0**（A0 只依赖 Q3 的默认①，若用户选②则 A0 顺延）；Q1/Q4 影响 A1 之后的路径选择。
