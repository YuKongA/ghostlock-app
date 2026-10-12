# 43499 · `select_stack` 移植研究（round 159，供计划用）

## 一、快照自带的机制说明（逐字 ✓）

```
select 特有：
  - 用 320 个 fd 的 fd_set 承载伪造 waiter 的字段（每个 64-bit word 落一处）；
    默认 waiter_shift = -2，即 fd_set word 0 对齐 waiter 起始；
  - 低编号 fd 被 dup2 成可读端，select 才会把这些 word 带进内核并 park；
  - 一个永不就绪的 timerfd 让 select 一直等，使陈旧 waiter 在整个 consumer 窗口内保持可被 walk；
  - compact(6.1) 与 6.6 两套 waiter 词表；compact 会重建 payload 页并重试。
```

## 二、快照的结构地图（行号 ✓，本树对应物 ✓）

| 快照 | 行 | 本树对应物 |
| --- | --- | --- |
| `WriteMode` / `WriteRequest` | 108-113 | `memory/write_layout.hpp` ✓（**已有** ✓） |
| `RouteStatus` / `RouteResultCode` | 114-130 | `seam/multicast_route.hpp` 的 report 结构 ✓ |
| `PiRace`（三演员） | 146-274 | `race/pi_race.hpp` + `pi_window.hpp` ✓（**已有** ✓） |
| `PageWriteLayout` | 275-315 | `memory/write_layout.cpp` ✓（**已有** ✓） |
| `FakePage` | 316-374 | `memory/payload_page.{hpp,cpp}` ✓（**已有** ✓） |
| `SelectRoute`（本 route 本体 ✓） | 375-560 | **新增**：`seam/select_stack_route.hpp` ✗ |

⇒ ⇒ **公共部分本树都已具备** ✓（引擎/页面/布局/竞态 ✓）⇒ 移植面 = **一个 route 文件 + 其 ops** ✓。

## 三、与 multicast 移植的差异（要点 ✓）

1. **拷贝路径不同**：multicast 走 `setsockopt(MCAST_BLOCK_SOURCE)` ✓；select 走 **`pselect` 的 fd_set** ✓；
   ⇒ 这正是它能当诊断的原因 ✓；
2. **驱动对象不同**：select 需要 **320 个 fd**（+ 低编号 dup2 可读端 ✓ + 一个永不就绪的 timerfd ✓）；
3. **词表两套**：compact(6.1) 与 6.6 ✓ ⇒ 本树设备是 5.15 内核 ✗ ⇒ **词表取法要按设备核实** ✓；
4. **失败时行为**：compact 会**重建 payload 页并重试** ✓（与 multicast 的 re-poison 同族 ✓）。

## 四、移植时的第一处需要核实（诚实标注 ✓）

- 设备内核 `5.15.189-android13-…` ✓ ⇒ 其 `rt_mutex_waiter` 布局需按该内核确认 ✓（快照只给了 6.1/6.6 ✓）；
- 本树已有 `MulticastWaiterLayout`（waiter_off/task_offset/lock_offset ✓）⇒ select 的 `waiter_shift = -2`
  对应到本树的那套偏移的方式，需在移植时**逐条核** ✓（不臆测 ✗）。
