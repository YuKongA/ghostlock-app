# 43499 · `select_stack` 移植第 1 步：route 核心 + 主机自测（round 168）✓

## 落地内容（加法式 ✓，未接线 ✗）

| 文件 | 内容 |
| --- | --- |
| `landing/select_stack_route.hpp`（新增 ✓） | `SelectStackConfig`（fd_words / waiter_shift / compact / fake_waiter_prio / fake_task / fake_lock / target / value ✓，**全走文档** ✓）、`SelectStackReport`（与 multicast 同形 ✓）、`select_stack_word_site()`（词 → (组, 词) ✓）、`select_stack_compact_words()`（11 词，与快照 414-430 **同序** ✓） |
| `tests/select_stack_route_test.cpp`（新增 ✓） | 主机自测：词 2→组0词0 ✓、词 12→组0词10 ✓、负位移被忽略 ✓、越界被拒 ✓、非 compact 不表 ✓、词值同序（tree/pi 双侧 + task/lock + prio 左移 32 后再或 3 ✓） |
| `core/Makefile` | 源列表 +1 ✓（`:282`）、新构建规则 ✓（`:510`）、进 `EVIDENCE_BINS` ✓（`:318` 尾） |

## 判据（本轮的 ✓）

```
build/host/select_stack_route_test ⇒ "select-stack route: word table and fd_set sites hold" ✓  TEST_RC=0 ✓
check → RC=0，108 PASS ✓（**未降** ✓）；android → RC=0，0 警 ✓；lint → 0 ✓
既有两条路（43499 三步 / 43284 底层 Lua）行为不变 ✓（本文件无调用者 ✗）
```

## 为什么这样切（纪律 ✓）

- 计划的第 1 步就是"**只加文件、不接线**" ✓ ⇒ 零行为风险 ✓ ✓，且**进构建** ✓（不是死代码 ✗）；
- 先落**纯函数**部分（词表 ↔ fd_set 词位 ✓）：它正是与 multicast 的**唯一机制差异**所在 ✓，
  且**主机可测** ✓ ⇒ 先把"词落到哪一组哪一个词"钉死 ✓，再接 pselect/演员 ✓；
- 动过的 Makefile 三处都用**精确字面锚点**改 ✓（不按行删/不整体写回 ✓）。

## 下一步（照计划 ✓）

1. 在 `SelectStackRoute<Ops>` 里接上 `pselect` + 三演员 + 低编号 fd/pipe/block 的生命周期（照 AGENTS §5 逐条列 ✓）；
2. **先按设备（5.15）核 `rt_mutex_waiter` 词表** ✓（快照只给 6.1/6.6 ✗，不臆测 ✓）；
3. 接线选择器 ⇒ 判据：夹具从 `route-not-ported:select_stack` ✓ 变成本 route 的报告 ✓；
4. 三级判据（多次统计 ✓）：奇偶/窗口 ✓ ⇒ `enforce` 1→0 ✓ ⇒ `uid=0` ✓。
