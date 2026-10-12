# 43499 · stamp 缺元组：已补（真缺口 ✓），但尚未翻（round 154）

## 缺口（有退役代码逐字为据 ✓）

```cpp
// 退役 payload_builder.cpp:34-45  encode_compact_waiter（两支）
preserve_child:  waiter+0x18 ← layout.right ; +0x20 ← 0 ; +0x28 ← request.target   ← 元组 ✓
else:            waiter+0x18 ← layout.parent ; +0x20 ← layout.right ; +0x28 ← layout.left
// 本树 multicast_arm.cpp:60-62  encode_multicast_arm：**只写 task/lock 两个指针** ✗
⇒ ⇒ stamp（**内核拷贝到栈上的那个缓冲** ✓）里**没有元组** ✗ ⇒ 内核搬过去的是"无元组的等待者" ✓
```

## 已补（本树 ✓）

```cpp
multicast_route.hpp build_stamp()：encode_multicast_arm 之后，按 preserve 支写三处 tuple：
  put_tuple(0x18, config_.value) ; put_tuple(0x20, 0) ; put_tuple(0x28, config_.target)
（preserve 判定用 `config_.value != 0` ✓ —— 本树 write_layout 的规则 ✓；
  0x18/0x20/0x28 是内核 rt_mutex_waiter 自身的位置 ✓，不是本树数据 ✓）
```

## 真机（3 跑 ✓，如实 ✓）

```
try1/3：奇偶不过 ⇒ 提前停 ✓（分布不变）
try2（完整）：main: selinux enforce=1 ✗   comm after len=15 ✗   uid=2000 ✗
⇒ **补了元组没有翻转结果** ✗ ⇒ 不夸大 ✓（但这是一处**真实的**、有据的缺口，已闭合 ✓）
```

## 下一处未弥合的差异（有退役注释为据 ✓）

```
退役 multicast_waiter_route.cpp：
   unsigned char stamp[stamp_size];     // ★ **调用帧的栈** VLA
   注释原文：The copy landing on the **caller's stack** is a race against skb page recycling
本树：stamp_ 是**引擎成员缓冲** ✗ ⇒ "拷贝落在哪张栈"这一点**从未对齐** ✗
⇒ 下一步候选（属攻击关键路径的结构改动 ⇒ 先设计再动 ✓）
```
