# 43499 · 旧码对照台账（第 22 轮，「瓶颈找旧代码」）✓✓

## 一、用途（✓）

```
瓶颈 = "写不落地（系统性 ✗）"。本页把**退役与本树的逐处对照**收敛成一页 ✓，
新增一条即登记一条 ✓；结论只有三种：已对齐 ✓ / 无差异 ✗ / 未实现或未对照 ✗。
```

## 二、台账（✓）

| # | 退役位置（`b0e9f499^`） | 本树对应 | 结论 |
| --- | --- | --- | --- |
| 1 | `steps.cpp:164` W2 调用（`leaf=0` ⇒ preserve=false ✓） | `main.cpp` W2 分支 | **已对齐** ✓ |
| 2 | `steps.cpp:342` W1 调用（`leaf=0` ✓） | `main.cpp` W1 分支 | **已对齐** ✓ |
| 3 | `steps.cpp:214` W3 调用（`leaf=1` ✓） | `main.cpp` W3 分支 | **已对齐** ✓ |
| 4 | `steps.cpp:86` `slab_drain` 于每阶段首轮 ✓ | `drain_slab_waves()` | **已实现** ✓ |
| 5 | `primitives.cpp:152` `attack_write` 的 `in_direct_map` 首查 ✓ | — | **未实现** ✗（唯一仍缺的守卫 ✓） |
| 5b | `spray.cpp:704/707` 页的**采纳判据**（臂不符 / 偶字节 ⇒ 换一页 ✓） | `main.cpp:886-889` 已有 `layout_accepts_page` 门 ✓ + `:880 gate probe` ✓ + `write_layout.cpp:35` 内含 `matches_request` ✓ | **已实现** ✓（第 35 轮更正 ✓：第 31 轮曾误判"缺失" ✗，因 grep 被 `head -10` 截断 ✗） |
| 6 | `primitives.cpp:157` 元组 `{pc=value,right=0,left=target}` ✓ | `encode_waiter_arm` | **已对齐** ✓ |
| 7 | `spray.cpp:355` `W0_OFF+0x00 = 1`（RED ✓） | `payload_page.cpp:102` `head_word = 1U` | **已对齐** ✓（第 21 轮查明 ✓） |
| 8 | `spray.cpp:356/357` `rb_right/rb_left = 0` ✓ | `head_next/head_prev = 0` | **已对齐** ✓ |
| 9 | `spray.cpp:364/365/366` task/lock/wake_state ✓ | `waiter.task/lock/wake_state` | **已对齐** ✓ |
| 10 | `route_middleware.cpp` = 仅转 `run_main_route_threads` ✓ | 本树路由调用 | **无差异** ✗（18 行 ✓） |
| 11 | `multicast_waiter_route.cpp:170/187` 预建/激活 ✓ | — | **未实现** ✗（计划已出 ✓ 第 12 轮 ✓） |
| 12 | `race/threads.cpp:228` `run_main_route_threads` ✓ | 本树路由编排 | **已对照** ✓（见下） |
| 13 | `race/threads.cpp:169` `reset_main_route_state` ✓（每次路由前重置 ✓ + `fast_repair ? 5000 : select_enter_delay_us` ✓ 并保留 `fast_repair` ✓） | `multicast_route.hpp:242` `shared_.reset()` ✓（**就在 `start_threads()` 之前** ✓，即每次路由调用都走 ✓）；`:160` `reset()` 清十个原子标志 ✓ | **已对齐** ✓（第 24 轮查明 ✓） |

> 第 24 轮附注（✓）：本树顺序 `… route-guard → build_stamp → shared_.reset() → route_delay_usec.store(...) → start_threads()` ✓，
> 与退役 `reset → start_threads` **同序** ✓ ⇒ 『跨轮状态残留』嫌疑**排除** ✗。
> 唯一**尚未实现**的仍是 `fast_repair` 那一路（第 11 条 ✓，预建/激活 ✗，计划见第 12 轮文档 ✓）。

### 第 12 条的对照结论（第 23 轮 ✓）

```
退役形状：reset → start_threads(waiter/owner/consumer) → race.run() → request_stop → join → return（31 行 ✓）；
本树形状：prepare → run_actors → execute → stop_actors（join ✓）⇒ 结构对应 ✓；
⇒ 唯一未对齐的**位置**是 reset 的**时机**（退役=每次路由调用前 ✓；本树待查 ✗）。
```

## 三、本轮新增的排除（✓）

```
`route_middleware.cpp` 全文 18 行 ✓，函数体就是 `return race::run_main_route_threads(request);` ✓
⇒ "中间件层"**无差异** ✗ ⇒ 不再在此处找理由 ✓。
```

## 四、下一目标（✓）

```
`race::run_main_route_threads`（退役 `race/threads.*` ✓）—— 本树尚未逐行对照 ✗；
重点看：① 线程就位顺序 ✓；② arm 序列与 settle ✓；③ `fast_repair` 标志的使用 ✓；
④ 退出/回收路径（守则 §5 ✓）。判据不变：任一轮 `main: victim uid=0` ✓。
```
