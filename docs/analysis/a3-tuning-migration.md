# A3 前置：GAP-2 / GAP-3 调参迁移进 profile（**只出设计**）

> 分析类（≤8 KiB）。**只出设计，不动代码**。依据：task-124 考古（GAP-2 硬编码清单）· `core/src/contract/op_registry.hpp`（GAP-3 限值）· `app/src/main/assets/profile/execution-*.conf`（现役键布局）· [rewrite-plan-v2](../archive/20261017-1835-rewrite-plan-v2.md) §4.1/§4.2。

## §1 键命名方案

- **统一规则**：**`backend.<backend>.execution.<scope>.<name>`**，`scope ∈ { stages · race · heap · handoff · routes.<route> · recommended_cpus }`。**现役资产已按此布局**（实测：`execution-tuning.conf` 的 `stages/race/heap/handoff/recommended_cpus`；`execution-select-stack.conf` / `execution-tcp-zerocopy.conf` 的 `execution.routes.<route>.*`）。
- **与已参数化的 30 个访问器兼容**：**不重命名旧键**（`w1_attempts`/`w1_settle_us`/`w3_chain_rounds`/`heap.prepare_max_attempts`/`race.*`/`handoff.*` 等保持原字面）；**新键沿用同规则**（新加者落 `stages` / `routes.<route>` 子节，或复用同名旧键）。
- **命名细则**：① 时间量后缀统一 `_us` / `_ms`（随现役惯例）；② 计数后缀 `_attempts` / `_rounds` / `_calls` / `_iterations`；③ **per-route 键必须落 `routes.<route>`**（否则换路即失配）；④ 键名**不得**含目标/符号名（与「禁止名字→值表」一致，见 [target 附件](lua-surface-annex-target.md) §C1）。

## §2 逐值键表（**15 行**；GAP-2 11 + GAP-3 4）

| # | 现值与来源（file:line） | 建议键名 | 类型/范围 | 可缺省 | 读层 | per-route | 批次 |
|---|---|---|---|---|---|---|---|
| 1 | 退避 **100 ms** · `steps.cpp:95` | `execution.stages.retry_backoff_ms` | Uint / 1–60000 | **否** | steps（race 面） | 否 | A3-B1 |
| 2 | 校验后 **50 ms** · `steps.cpp:103` | `execution.stages.verify_settle_us` | Uint / 0–1e6 | 否 | steps | 否 | A3-B1 |
| 3 | park-retry **50 ms** · `steps.cpp:118` | `execution.stages.park_retry_settle_us` | Uint / 0–1e6 | 否 | steps | 否 | A3-B1 |
| 4 | W3 重试 **100 ms ×2** · `steps.cpp:247` · `:258` | `execution.stages.w3_retry_settle_us` | Uint / 0–1e6 | 否 | steps | 否 | A3-B1 |
| 5 | `kMulticastArmHold=20000` · `multicast_waiter_route.cpp:36` | **`execution.routes.multicast_waiter.arm_hold`** | Uint / 1–1e6 | 否 | route | **是** | A3-B2（**依赖 GAP-1**） |
| 6 | `PSELECT_CONSUMER_SETTLE_USEC=250000` · `memory/constants.hpp:43` | `execution.routes.select_stack.consumer_settle_us` | Uint / 0–1e7 | 否 | route | **是** | A3-B1 |
| 7 | `usleep(1000)` · `select_stack_route.cpp:75` | `execution.routes.select_stack.poll_interval_us` | Uint / 1–1e5 | 否 | route | **是** | A3-B1 |
| 8 | `usleep(20000)` · `primitives.cpp:58` | `execution.race.state_poll_interval_us`（**复用现役直同名键**） | Uint / 1–1e6 | 否 | race | 否 | A3-B1 |
| 9 | `sample_period=5000` · `primitives.cpp:68` | `execution.race.sample_period_us` | Uint / 1–1e6 | 否 | race | 否 | A3-B1 |
| 10 | 线程超时步 **20 ms** · `race/threads.cpp:45` | `execution.race.thread_timeout_step_us` | Uint / 1–1e6 | 否 | race | 否 | A3-B2 |
| 11 | nice 轮转 `(calls%19)+1` · `race/threads.cpp:141-143` | `execution.race.nice_rotation` | Bool（开/关）+ 取模基数走 `stages.nice` | 否 | race | 否 | A3-B2 |
| 12 | 泄漏轮询 **50 ms** · `spray.cpp:548` | `execution.race.leak_poll_interval_ms` | Uint / 1–60000 | 否 | race/spray | 否 | A3-B2 |
| 13 | `attempts` 缺省 **8** · `op_registry.hpp` `kWindowW1W3Params`（`required=true`） | `execution.stages.w3_attempts`（**旧键同名**） | Uint / 1–64 | **否**（required） | op_registry → steps | 否 | A3-B0 |
| 14 | `attempts` 缺省 **8** · `op_registry.hpp` `kWindowW1W2Params`（`required=false`） | `execution.stages.w2_attempts` / `w1_attempts`（旧键） | Uint / 1–64 | 是（`false` 时可缺省） | op_registry → steps | 否 | A3-B0 |
| 15 | `burst_calls` **64** · `settle_us` **200000** · `kPagecacheParams` | `stages.pagecache_burst_calls` · `stages.pagecache_settle_us` | Uint / 1–1024 · 0–1e7 | 是 | op_registry → route | 否 | A3-B1 |

- **键名理由（2026-10-08 裁决「按推荐」）**：**与 wire/manifest 同名**（已在 `profile-manifest-v3.tsv`）；§1 规则即**不重命名旧键**；**tcp 先例 `post_receive_hold_iterations` 同名** ⇒ **三处同名**（满足「表达一致」）。
- **口径说明（如实）**：任务口径 = 「11 + 4 = **15 行**」。实测 **site 计数 = 13 + 4 = 17**（`:247/:258` 同值两处按**一键一行**计 ⇒ GAP-2 = **12 键 / 13 site**；GAP-3 的 `burst_calls`+`settle_us` 同表并列 ⇒ **4 键**）。本表按「**一键一行**」给出 **15 行**：其中第 4 行覆盖 2 处 site、第 15 行覆盖 2 个键（`burst_calls`/`settle_us`）⇒ 若按「一 site 一行」则为 17 行。**两口径均已在本表可还原**。

## §3 缺省语义（**缺键 ⇒ 具名拒绝**）

- **规则**：延续 [rewrite-plan-v2](../archive/20261017-1835-rewrite-plan-v2.md) §3 / Q2「**数值不入 C++、不兜底**」⇒ **缺键即拒绝**，不得使用 C++ 缺省值；`op_registry.hpp` 的 `ParamSpec.default_value` **仅作文档/对拍基线**，不作为运行期兜底（其 `required=false` 的项允许**在脚本层**省略，由 profile 提供值）。
- **建议拒绝码**：**`tuning-missing`**（附 `path=backend.<b>.execution.<scope>.<name>` 与 hint）；与既有码分工：`geometry-missing` = 几何/偏移缺失 · **`tuning-missing` = 调参键缺失** · `param-invalid` = 值越界 · `param-missing` = **脚本调用**缺参。
- **判据（可失败）**：① 删掉 `retry_backoff_ms` ⇒ 43499 链在 steps 首步即 **`tuning-missing`**（不是 0/默认值）；② 键值越界（如 `0`/`1e9`）⇒ `param-invalid`；③ 在 `routes.tcp_zerocopy` 下缺 `arm_hold` ⇒ 仅 tcp 拒（其他 route 不受影响）。

## §4 边界与例外（**不该迁**，逐条理由）

| 类别 | 条目 | 不迁理由 |
|---|---|---|
| 机制常量 | 页大小 `4096` | **协议/结构本身**（页粒度是内核 ABI，不是可调参数） |
| 机制常量 | futex 码（`FUTEX_*` / `PI` 位） | 内核 ABI 值，改即改协议 |
| 机制常量 | 结构偏移（`task_struct`/`cred` 布局） | **已属 `cfg` 数据面**（profile 声明）+ 运行期换算；C++ 只留「机制换算」，不留名字→值表 |
| 机制常量 | waiter 结构 `0x30`（尺寸/字段位） | 漏洞原语的结构契约（与 race 机制绑定），非调参 |
| 机制常量 | `WriteMode` 枚举 · kind 枚举（`kernel_offset`/`absolute`/`direct_map`） | 接口形态（选的是**语义**），须注释「为何属机制」 |
| 运行期量 | 目标绝对地址 | 只能运行期观测（`基址 + 偏移`），配置化即失真 |
| 运行期量 | 喷溅命中页 · 泄漏 task · waiter 地址 | 运行期事实（每次尝试都可能变），不可预置 |
| 运行期量 | pid / 会话密钥 / SPI / 端口 | 运行期密钥类，**永不进文档/配置**（走会话帧，用后清零） |

## §5 与 A3 的关系（必须先就位的键 + GAP-1 交叉）

- **A3 落地前必须就位（否则 43499 链跑不起来）**：**`retry_backoff_ms`** · **`verify_settle_us`** · **`park_retry_settle_us`** · **`w3_retry_settle_us`** · **`w3_attempts`**（旧键已在 `stages`）· **`select_stack.consumer_settle_us`** · **`select_stack.poll_interval_us`** · **`race.state_poll_interval_us`** ⇒ **8 键**（缺一即链失败，按 §3 具名拒绝）。
- **可随批次补**：`race.sample_period_us` · `race.thread_timeout_step_us` · `race.nice_rotation` · `race.leak_poll_interval_ms` · `pagecache_*`（43284 链，不阻塞 43499）。
- **与 GAP-1 交叉（**2026-10-08 已关闭**）**：`execution-multicast-waiter.conf` **资产已在** ✓（**1606 B** · 提交 **`7b34bb44`**）⇒ 第 5 行键可**随 A3 批次直接落**（原「依赖 GAP-1 先补齐」的前提消失）；`execution-tcp-zerocopy.conf` 仍**无 `arm_hold`** ⇒ 若 tcp 侧用到则一并补。
