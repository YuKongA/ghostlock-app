# 43499 · "重试拿同一块"的根因追到一行（round 126）

## 物证（读码 ✓）

```cpp
// core/src/backend/cve_2026_43499/memory/prepare_page.hpp:478
outcome.base = align_base(leaked, geometry.align_region_size);
```

⇒ `base` 不是"泄漏地址"本身 ✗，而是它**按 region 对齐量化**后的结果 ✓ ⇒ 同一个 `base` 至少两种来源：

1. 泄漏没变（同一次 spray 的同一 slot ✗）；
2. **泄漏变了，但被对齐量化到同一格** ✗（更可能 ✓ —— 也解释了"4 次都同址" ✓）。

## 所以奇偶问题的正解（顺序 ✓）

1. **让泄漏地址粗粒度变化**（比 `align_region_size` 更大 ✓）⇒ 对齐后的 base 才换格 ✓；
2. 本树可做的等价物（按代价排）：
   - a. 重试之间**重开 shaping/peer 资源**（等价退役的 release ✓）⇒ 若对齐格够大可能仍不动 ✗；
   - b. **换一块更大的 spray 区/换 mm**（等价退役的 quarantine 全流程 ✓）⇒ 更可能真换格 ✓；
   - c. 直接把 `align_region_size` 的取值与 `payload_delta` 一起纳入奇偶推导 ✓（不改资源 ✗，但属改语义 ✗）；
3. 先试 a ✓（改动最小 ✓、可回退 ✓），不行再上 b ✓（有退役为据 ✓）。

## 本轮状态

- 奇偶判据、预检算法、三步三请求：**全部正确** ✓；
- 唯一卡点：**地址轮换** ✗（根因已定位到 `align_base(leaked, align_region_size)` ✓）；
- `uid=0`：仍未 ✗。
