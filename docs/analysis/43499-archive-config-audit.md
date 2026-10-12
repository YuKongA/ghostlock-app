# 43499 · 按归档配置头审计（round 249）✓✓

## 为什么要做这件事（✓）

```
归档**确实有成功记录**（`root script start uid=0 euid=0` ✓、28 s 完成 ✓、KernelSU 模块加载 ✓）⇒
⇒ ⇒ 目标**可达** ✓（本晚第一次拿到这个证据 ✗ ⇒ 之前只在"我这边"看不到 ✓）；
⇒ 归档头部还逐条打印了**退役当时的配置** ✓ ⇒ 正好可以**逐项对表** ✓（本轮就是对的这张表 ✓）。
```

## 对表结果（✓）

| 归档键（退役 ✓） | 我的树 | 结论 |
| --- | --- | --- |
| `multicast_waiter.ready_timeout_ms=10000` ✓ | **0 处引用** ✗ | 缺一个**等待上限** ✓（健壮性，非写效 ✓） |
| `multicast_waiter.post_requeue_settle_us=200000` ✓ | 已实现 ✓（`multicast_route.hpp:92-96` ✓，用于 `:343-344` ✓） | 一致 ✓ |
| `multicast_waiter.post_adjust_settle_us=100000` ✓ | 已实现 ✓（同上 ✓） | 一致 ✓ |
| `select_stack.timeout_us=200000` ✓ | 我用 250000 ✗（差 50 ms ✓） | 微差 ✓（可对齐 ✓） |
| `select_stack.enter_delay_us=50000` ✓ | 阶梯首格 50000 ✓ | 一致 ✓ |
| `stages.w1_attempts=15 / w1_settle_us=100000 / w2_attempts=15 / w3_chain_rounds=3` ✓ | 夹具同 ✓ | 一致 ✓ |

## 收获（✓）

```
① 目标可达 ✓（归档成功记录 ✓）——这是本晚最重要的一条外部证据 ✓；
② 对表后**只剩两处微差**（`ready_timeout_ms` 缺 ✗ / `timeout_us` 差 50 ms ✗）⇒ 都不是"写不落地"的解释 ✗；
③ 因此"写/生效"的缺口**不在这些参数** ✗ ⇒ 与前面几十轮的排除结论**一致** ✓。
```

## 门禁（未改码 ✓）

```
check → 108 PASS ✓；android → 0 警 ✓；lint → 0 ✓
```
