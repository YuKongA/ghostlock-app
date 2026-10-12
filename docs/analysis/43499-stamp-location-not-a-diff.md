# 43499 · 更正理解：stamp 的"栈"不是差异（round 156）

## 退役注释的真正含义（重读 ✓）

```
"The copy landing on the caller's stack is a race against **skb page recycling** …"
⇒ 那个 "copy" 是**内核**做的 ✓：内核从 **skb**（我们的 sent page ✓）拷到**内核自己的栈** ✓；
⇒ "race" 指的是 **skb 的页面被回收**（我们那块页得还在 ✓）⇒ 与"我们把缓冲放在哪张栈"**无关** ✗。
⇒ ⇒ 因此："退役 VLA vs 本树成员缓冲" **不是差异** ✗（两者都只是"被拷进 skb 的字节源" ✓）
```

## 佐证（本树 ops 层逐字 ✓）

```cpp
// multicast_route_linux.hpp:133
static int set_block_source(int fd, const uint8_t *buffer, size_t size) {
    errno = 0;
    return ::setsockopt(fd, IPPROTO_IP, MCAST_BLOCK_SOURCE, buffer, (socklen_t) size);
}
⇒ 只是把调用方的缓冲递进去 ✓ ⇒ 位置无关 ✓（内核 copy_from_user 只认字节 ✓）
```

## 于是：可比对的清单**已全部闭合** ✓

| 层级 | 状态 |
| --- | --- |
| 代码 / 常数 / 几何 / 数值 / 字段 / 字节 / 判据语义 | 对齐 ✓ |
| stamp 的两指针 + 元组（本轮之前补 ✓） | 对齐 ✓ |
| 每轮 re-poison（本轮之前补 ✓） | 对齐 ✓ |
| 缓冲位置（VLA vs 成员） | **不是差异** ✓（本回合理清 ✓） |
| 结果 | `enforce=1` ✗、`uid=2000` ✗ 仍不变 ✗ |

## 结论（供决策 ✓）

- 能对照的层级**已经见底** ✓；⇒ 剩下的只有此前的两条大路 ✓：**换 route** 或**内核侧观测**（需 root ✗）；
- 以及一条便宜的编排：**多次运行直到奇偶合格** ✓（约 1/3~1/2 ⇒ 每几跑能完整跑一次 ✓）；
- 我这边不会在无据的情况下继续改机制 ✗（今晚已两次自我更正 ✓，说明"看起来像差异"的东西必须逐条验证 ✓）。
