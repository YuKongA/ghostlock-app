# 43499 · waiter 健康但"不在 wait 队列上"（round 220）✓✓

## 阶段读数（✓，来自 waiter 线程内部 ✓）

```
main: waiter locked=0 ✓   chain=0x282e ✓   ← 拿到了 chain，值 = 自己的 tid ✓
main: waiter ready tid=10 ✓
main: waiter go ✓
main: waiter pre-park wait=0x0 target=0x0 ✓
… 之后：park 返回 -1 / errno=110（ETIMEDOUT ✓）⇒ 它确实在等，而 requeue 报 ret=0 ✗
```

## 结论（二选一，读数已排除大半 ✓）

| # | 解释 | 现状 |
| --- | --- | --- |
| 1 | **requeue 早于 waiter 入队** ✗ | 最可能 ✓：pre-park 打印之后到真正入队之间仍有窗口 ✓ |
| 2 | requeue 用的**不是同一个字** ✗ | 可能性低 ✓：两侧都打印了 `wait=0x0` / `chain=0x…`，且传参同一来源 ✓ |

## 与快照的对照（决定性 ✓）

```
快照 :242-243：`while (!waiter_waiting || !owner_started) ::usleep(100);` 之后 `::usleep(1000)` ✓；
⇒ 它的 `waiter_waiting` 是**在 `wait_requeue_pi` 之前**置位的 ✓（与我的 `ready` 同型 ✓）；
⇒ ⇒ 但它还有**第二个**标志 `owner_started` ✗ —— 即"owner 已经拿到 target" ✓；
⇒ 我的执行体里 **owner 侧拿 target 是 `PiRace::run` 内部**（并**忽略返回值** ✗）⇒ 没有这个同步点 ✓。
```

## 下一步（下一批 ✓，两点都要做）

```
① 把 `PiRace::run` 里 `lock_pi(target)` 的返回值**暴露出来**（或在本 route 侧自己先拿一次 target ✓）
   ⇒ 确认 owner 真的持有 target（不然内核不会把等待者搬上去 ✓）；
② 在 requeue 之前**再等一小段**（快照的量级：100 µs 轮询 + 1 ms 沉降 ✓）⇒ 排掉"早于入队" ✓。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
