# 43499 · quarantine 的真身：页面状态三槽（round 129）

## 退役实现（spray.cpp:227-258 逐字 ✓）

```cpp
close_reclaim_sockets()               → heap.current.destroy();
quarantine_reclaim_sockets()          → heap.current.move_to(heap.quarantine, Quarantined);   // 不销毁 ✓
release_quarantined_reclaim_sockets() → heap.quarantine.destroy();
stash_prebuilt_page()                 → heap.current.move_to(heap.prebuilt, Prebuilt);
activate_prebuilt_page()              → ① prebuilt.has_reclaim() ② close_reclaim_sockets()（销毁 current）
                                        ③ prebuilt.move_to(heap.current, Current);
```

⇒ 三个槽：`Current` / `Quarantined` / `Prebuilt` ✓（`PayloadPageState` ✓）；
⇒ "预置页"的用法是：**先建好第二页（prebuilt）→ 再销毁第一页（current）→ 把第二页搬回 current** ✓。

## 对"重试拿同一块"的直接含义

- 我这两轮都在 `run_prepare` 的**外层**绕（关 fd / 留 fd / 清 base ✗）⇒ 三种做法都不是它的机制 ✓；
- 真正的机制在 **`prepare_good_kernel_page` 怎么挑 base** 里 ✓（= base 从哪来、什么条件下会变 ✓）。

## 下一步（单点 ✓）

读退役 `prepare_good_kernel_page`（挑 base 的算法 + 失败条件 ✓）⇒ 对照本树 `prepare_page.hpp` 的
`align_base(leaked, align_region_size)` ✓ ⇒ 定位"同一 base"的确切原因（泄漏不变 ✗ 还是对齐量化 ✗）；
⇒ 判据：让三步各自拿到**奇** base（`accepts=1` ✓，不依赖重试 ✓），随后回到 `uid=0` ✓。
