# 切片 1：multicast 窗口体 22 处旧会话耦合的替换表

- 2026-10-09 · 依据 `seam-port-design.md` §6 · 取材只读 `git show b0e9f499^:<path>`
- **计数 = 按行**：`…multicast_waiter_route.cpp | grep -c "cve43499_state("` = **22**（同设计稿 §6；L69 一行 2 次访问 ⇒ 22 行 / 23 个表达式）。下表 22 行 = 一旧行号一行。
- 类：**M** 可映射 · **D** 删 · **X** 无法映射（理由 §2）。旧件 **M** = `src/core/backend/cve_2026_43499/route/multicast_waiter_route.cpp`（208 行 @`b0e9f499^`）；`threads.cpp` = `src/core/race/threads.cpp`（同 rev）

## 1 替换表（22 行，逐行可核）

| # | 旧 file:line | 旧写法 | 新写法（目标） | 取值来源 / 缺口 | 类 |
|---|---|---|---|---|---|
| 1 | M:47 | `…profile.multicast_layout()` 读几何 | `memory::MulticastGeometry`（arm:20-25，四字段同名） | 键名权威 keys:63-66；**值按 ADR-0015 由 Lua 传**，新树无载体（ap:31-35 只 route/offset/offset_set）⇒ 缺口 | M |
| 2 | M:69 **×2** | `.heap.current.fake_task` / `.heap.current.fake_lock` | `TargetState::fake_task`（se:28）/ `::fake_lock`（se:30） | TargetState（bring_up 已留存 sm:53）；落地目标 arm:62-72 | M |
| 3 | M:92 | `.race.consumer_calls.store(0)` | 删 | 概念只有只读 `calls()`（mp:29），无 reset 面 ⇒ 归零无对象 | D |
| 4 | M:93 | `.race.consumer_success.store(0)` | 删 | 同理（`successes()` mp:28） | D |
| 5 | M:94 | `.race.consumer_stop.store(0)` | 删 | 消费线程内部位（旧读者 threads.cpp:114）⇒ 语义随 consumer 实现 | D |
| 6 | M:95 | `.race.route_delay_usec.store(0)` | 删 | 同上（旧读者 threads.cpp:129）；Limits（mp:39-44）无此字段 | D |
| 7 | M:102 | `…profile.mcast_tuning()` → attempts/arm_sequence/arm_hold | `MulticastLimits::max_attempts`、`::arm_hold`（mp:41/43） | 键名 keys:60-62；值由 Lua 传（缺口同 #1）。**第三值 `arm_sequence` 无字段** ⇒ §2 | M* |
| 8 | M:123 | `.race.consumer_go.store(attempt)` | `consumer.arm(attempt)`（mp:25；调用 mp:87） | 概念实参 | M |
| 9 | M:126 | `.race.consumer_go.store(0)` | `consumer.disarm()`（mp:26；调用 mp:89） | — | M |
| 10 | M:127 | `.race.consumer_inflight.load()` | `consumer.inflight()`（mp:27；循环 mp:90-94） | — | M |
| 11 | M:129 | `.race.consumer_success.load() > 0` | `consumer.successes() > 0`（mp:28；mp:95-97） | — | M |
| 12 | M:141 | 同上（旧「只有它才算 OK」的唯一依据） | `MulticastOutcome::successes > 0` ⇒ `Armed`（mp:71、99-101） | — | M |
| 13 | M:152 | 日志读 `.race.consumer_calls.load()` | 删 | 窗口内不得 logging（pw:43）；读数移到窗口外 `MulticastOutcome::calls`（mp:72） | D |
| 14 | M:153 | 日志读 `.race.consumer_success.load()` | 删 | 同上（mp:71）；pr_info 本体（M:148-153）移出窗口 | D |
| 15 | M:173 | `.addresses.data_alias(…)` | `KernelAccess::write` 的 `address` **实参**（ka:188），非 TargetState/AccessParams 字段 | Lua（ADR-0015 §3）；新树 `data_alias`／`ResolvedAddresses` 各 0 命中 ⇒ 不重建；**地址算术不移植** | M |
| 16 | M:174 | `.addresses.init_cred_image_addr()` | 同 #15（同表达式的内层调用） | 同 #15 | M |
| 17 | M:176 | `.heap.current.base = spray::prepare_good_kernel_page(req)` | `TargetState::page_base`（se:26） | prepare 步骤填充（prepare_page.hpp、heap_seam.hpp） | M |
| 18 | M:178 | `.heap.current.base` 读回判空 | `TargetState::page_base` + `write_readiness()`（se:60-71） | 同上；缝侧已有同款守卫 sm:75-77 | M |
| 19 | M:194 | `.addresses.data_alias(…)` | 同 #15 | 同 #15 | M |
| 20 | M:195 | `.addresses.init_cred_image_addr()` | 同 #16 | 同 #15 | M |
| 21 | M:198 | `.race.fast_repair.store(1)` | **无归属** | §2 | **X** |
| 22 | M:200 | `.race.fast_repair.store(0)` | **无归属** | §2 | **X** |

## 2 无归属 / 未闭项（逐条给理由，不硬凑）

**X1 · M:198 / M:200（`race.fast_repair` store 1/0）**

1. 新树无运行期 latch：`grep -rn fast_repair core/src` 只命中 rp:30 的编译期能力位（`w2_fast_repair = false`），无字段可写。
2. 旧读者是 waiter 线程（threads.cpp:44）：fast ⇒ 固定 20 ms 超时，否则 `profile.race_route_wait_ms()` ⇒ 它是竞速时序两档选择；新树时序面 rr:20-33（settle_us/poll_us/poll_rounds）**没有**该两档。
3. 它服务 steps 层的 W2 快速修复（旧 `steps.cpp:89,99`）**不属窗口体** ⇒ 不硬凑。落地按 ADR-0015 作新决策（档位是否存在、值由 Lua 给），**不得凭旧默认值偷偷带回**。

**M* 行内未闭项 · M:102 的 `arm_sequence`（键名权威 keys:61）**

- `MulticastLimits`（mp:39-44）只有 max_attempts / arm_hold ⇒ 无字段。
- 其用途在**未耦合**分支 M:119（`attempt < arm_sequence` 暖场轮：只发 setsockopt、**不** arm consumer），新件 `run_multicast_handshake`（mp:85-98）**每轮都 arm** ⇒ 暖场语义无对应物。
- ⇒ 仍计入「M:102 可映射、第三值待定」（归属明确：route 侧起手等待轮数），**不升 X**；但**切片 2 定形状前须先裁决暖场是否保留**，否则不等价。
- 附带：旧 `0 = 用代码内默认`（M:38-40 + M:34-36 的 128/16/20000）在新件是**具名拒绝**（mp:82-84），且**代码内不得有调参数**（pw:45）⇒ 值一律 Lua 给，缺就拒（ADR-0015 §4.3），**不得回退 profile**。

**分类口径（供评审平移）**：#15/16/19/20 判 M 的口径是「耦合 = 从旧会话取值；新形状里该值有唯一实参入口（ka:188）」。若评审认为地址算术消失应记 X，则 **X = 6、M = 10**，证据不变。

## 3 新件锚点（本树实测；§1 的引用行号即为实测行号）

根 `core/src/`；除 ka 外均在 `backend/cve_2026_43499/` 下：

ka=`contract/kernel_access.hpp` · bk=`kernel_access.hpp` · se=`session.hpp` · ap=`access_params.hpp` ·
sm=`seam/multicast_seam.hpp` · mp=`route/multicast_policy.hpp` · keys=`route/expected_keys.hpp` ·
arm=`memory/multicast_arm.hpp` · rs=`route/route_status.hpp` · rp=`route/route_policy.hpp` ·
pw=`race/pi_window.hpp` · rr=`race/race_runtime.hpp` · wr=`memory/write_request.hpp`

## 4 结构耦合（**不计入 22**，同批必须处理）

- M:2 `#include "…cve_2026_43499_state.hpp"`（共享会话切出的字节缓冲+访问器）⇒ **删**（se:2-8 已记：冻结字段序的理由随 cmp_disasm 降级消失）
- M:18 `#include "memory/payload_builder.h"` ⇒ **删**（新树无此头；落在 wr:30-34 与 memory/write_encoder.hpp）
- M:19 `#include "…/route/route_status.h"` ⇒ **改路径**（rs:21-55 已移植）
- M:20 `#include "session/core_session.hpp"` ⇒ **删**（`grep -rn CoreSession core/src` = 0）
- M:21 `#include "memory/target.h"` ⇒ **删**（新树无此头；旧 direct-map 上限=第二个全局，边界改由纯函数取参数：perf_vote.hpp:42、scan_geometry.hpp:53）
- M:171 / M:188 形参 `session::CoreSession &exploit_session` ⇒ **删形参**（目标事实走 se:22-31，地址走 ka:188 实参）

## 5 结论 · 验收 · 不猜

- **行级计数：M 14 · D 6 · X 2**（合计 22；行内未闭项 1 处 = `arm_sequence`）。
- 本片**不改任何生产代码**；上表是切片 2 的输入。
- 验收：`make -C core check` **EXIT=0**（2026-10-09，落盘前跑；日志 `/tmp/seamport_check.log`）。
- 不猜（切片 2 定形状前必须回答）：

  1. `arm_sequence` 暖场是否保留（§2）——保留则 `MulticastLimits` 需加字段；
  2. `MulticastGeometry` / `MulticastLimits` 的值载体：`AccessParams` 加字段，或经 `bring_up` 实参（ADR-0015 §4.3：没传就按名拒，**不得回退 profile**）；
  3. **窗口体不吃 WriteRequest**：M:44 实测 `(void) request;`（设计稿 §4.3「构造 WriteRequest → 调 do_multicast_fake_lock_route」在 multicast 上不成立）；request 由 race 侧线程读（旧 pi_race.h:87、threads.cpp:33）⇒ `Window::write_word(address,value,offset)` 如何落到那次写，属切片 2。
