# 43499 写落地 · 与归档成功日志的对标（2026-10-11 夜）

## 本轮做到的（真进展，可复核）

- 本树判据 `layout_accepts_page` 前后：`matches=1 accepts=0` ✗ → **`matches=1 accepts=1`** ✓；
  - 原文原因：Zero + preserve_child 时要求 `((value) >> 16) & 1 == 1`，否则会清掉 `selinux_state` 的
    `initialized` 位（"breaks every subsequent SID lookup"）；
  - 修法：`WordsRegion` 扩到 128 KiB，并新增 `payload_for_zero_write()` —— 按 64 KiB 扫描挑出满足奇偶的那一页；
    同时在 main 里**尊重判据**（不满足即具名拒绝 `payload-layout-not-accepted`，不再默默继续）。
- 但仍**未落地**：`comm after len=15`（写零字节）✗、`victim uid=2000` ✗。

## 与归档成功日志的对标（关键）

归档文件：`docs/archive/device-gates/20260922-2231-CPP17-20260918-multicast-pass-2.native.log`（Oct 8，成功）

```
child_task=0xffffff898138da00
W2: cred attempt 1/15
W2: cred === target=0xffffff898138e198 mode=2 le…     ← target = child_task + 0x798(=1944) ✓ mode=Credential ✓
root! ／ uid=0

── 对照我的运行 ──
main: route status=0/0 attempts=2 calls=4 successes=4 stamp=-1/99 requeue=0 waiter=-1 owner=0
```

归档里还有三个**我配置里没有的节拍旋钮**：

```
waiter.ready_timeout_ms=10000
waiter.post_requeue_settle_us=200000
waiter.post_adjust_settle_us=100000
```

- 且成功那次的 `CMP_REQUEUE_PI` **也是 `ret=-1 errno=35`（EAGAIN）** ⇒ 我这边的 `requeue=0` **不是**决定性的 ✗；
- 差别更可能在那三个节拍（以及 `waiter parked; owner started` 那套三角色日志所对应的编排）上 ✓。

## 下一步（一次探测，不猜）

1. 把我的引擎**每次尝试**的数字（stamp/requeue/waiter/owner + attempts）与归档逐项并排；
2. 查本树引擎是否会消费 `post_requeue_settle_us` 一类节拍（`MulticastRouteConfig` 里有无对应字段）；
3. 若缺，按"退役键名 + 真值"补进 wire/schema（沿用既有做法）；
4. 判据仍只有两条：`comm after` 变 0/8（写落地）与 `victim uid=0`（root）——**两条都要**。
