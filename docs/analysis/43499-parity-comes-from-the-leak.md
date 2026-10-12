# 43499 · 奇偶的真正来源：被泄漏的 mm_struct（round 132）

## 退役代码（spray.cpp:585-600 逐字 ✓）

```cpp
const uintptr_t leaked = discovery.ok ? discovery.mm_struct : -1;   // 泄漏 = **mm_struct 本体**
if (leaked == -1 || leaked < KERNELSNITCH_IDENTITY_START || leaked >= g_direct_map_end) { …失败清理… }
uintptr_t base = leaked & ~(ORDER3_SIZE - 1);                       // base = 泄漏值按 32 KiB 对齐
```

⇒ ⇒ **奇偶 = 那个 mm_struct 的 bit16** ⇒ 而 mm_struct 是 **spray 主动放上去的对象** ✓
⇒ 所以退役 6/6 `ok attempt=1`（零 `taking another`、零 `retry` ✓）**不是运气** ✓，
  而是它把"泄漏目标"放在了**合格的位置** ✓。

## 归档的对照（决定性 ✓）

```
prepare_kernel_page ok attempt=1   → 6/6
taking another / retry             → 0/0
⇒ 若纯靠概率：6 次全中 ≈ 1/64 ✗ ⇒ 只能是"被安排" ✓
```

## 本树与退役的差别（下一步的落点 ✓）

| 项 | 退役 | 本树 |
| --- | --- | --- |
| `base` 来源 | `leaked & ~(ORDER3_SIZE−1)`（32 KiB ✓） | `align_base(leaked, page_bytes × 8)`（更粗 ✗） |
| 泄漏对象 | `discovery.mm_struct`（snitch 主动放置的对象 ✓） | 同一个量 ✓（`prepare_page.hpp:478` ✓） |
| **能否挑选** | 通过 spray 的**分阶段放置**（`pre/post/spray` 三组 ctx ✓ + quarantine ✓）决定其落在哪 ✓ | 未挑选 ✗ |

## 下一步（单点 ✓）

1. 读本树 `prepare_page.hpp` 的泄漏**来源**（snitch 目标怎么选 ✓）与退役 `kernelsnitch` 的差异 ✓；
2. 找"决定 mm_struct 落在哪"的那个旋钮（三组 ctx 的规模 ✓ / 顺序 ✓ / 是否 quarantine ✓）；
3. 判据：三步各自 `accepts=1` ✓ 不依赖重试 ✓；随后回到 `uid=0` ✓。
