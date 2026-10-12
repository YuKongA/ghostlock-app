# 43499 · 规模（组数/上限）确实抬高奇偶命中率（round 135）

## 真机数据（同一夹具，顶到本树上限 32 ✓）

```
run1: 3 × accepts=1 ⇒ 链跑完（uid=2000 ✗）
run2: 1 × accepts=0 ⇒ refused (payload-layout-not-accepted)
run3: 3 × accepts=1 ⇒ 链跑完（uid=2000 ✗）
⇒ 完整率 **2/3** ✓（此前组数=1/2 时约 1/8 ✗）
```

## 发现的结构差异（新 ✓）

| 项 | 退役 | 本树 |
| --- | --- | --- |
| groom 规模 | `pre = 8 × mm_objs_per_slab = **256**` ✓、`spray = (1+5) × 32 = **192**` ✓ | **上限 32** ✗（`prepare_page.hpp:38 kMaxPrepareChildren = 32` ✓）|
| 语义 | `pre_groups` = 子进程数 ✓（本树同 ✓：`for i < plan.pre_groups` 逐个 spawn ✓） | 同 ✓ |

⇒ 我的规模只有退役的 1/8 ✗ ⇒ 而"越多子进程 ⇒ 泄漏越可能落在被 groom 的区域 ⇒ bit16 越可能有偏 ✓"，
  2/3 的实测与此一致 ✓。

## 下一步（一条，属代码/容量改动 ⇒ 先计划 ✓）

1. 抬 `kMaxPrepareChildren`（32 → ≥ 256 ✓）并同步数组尺寸（`pre_pids/pre_fds/spray_*/post_*/leak_*` ✓）；
2. 夹具把 `pre_groups=256` ✓、`spray_groups=192` ✓（退役式 ✓）；
3. 判据：**连续 3 次**跑均 `w1/w2/w3 accepts=1` 且无 `payload-layout-not-accepted` ✓ ⇒ 再回到 `uid=0` ✓。
