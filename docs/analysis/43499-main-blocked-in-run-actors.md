# 43499 · 主线程在 `run_actors` 内被堵约 2 s（round 222）✓✓

## 时序（最新构建 ✓，按文件顺序 ✓）

```
main: sel-exec attempt ✓
main: waiter locked=0 chain=0x6da0 ✓  →  waiter go  →  waiter pre-park
main: waiter **parked=-1 errno=110** ✗（等满 2 s ✓）
main: sel-exec words-in wait=0x0 **chain=0x0** target=0x0   ← 此刻 waiter 已释放 chain ✓
⇒ ⇒ 主线程"`run_actors` + 50 ms settle"这段花掉了 **≥2 s** ✗ ⇒ 被堵在 `run_actors` 自身 ✓
```

## `run_actors` 的四个子步（下一批逐个打点即可定 ✓）

| # | 子步 | 是否可能堵 ~2 s |
| --- | --- | --- |
| 1 | `pthread_create(&waiter, …)` | 不太可能 ✗ |
| 2 | `pthread_create(&consumer, …)` | 不太可能 ✗ |
| 3 | `while (waiter_ready_ == 0) Ops::yield();` | **可能** ✓（若 waiter 尚未置位 ⇒ 自旋 ✓） |
| 4 | `Ops::sleep_micros(1000)` | 不会 ✗ |

## 本轮已做的一处清理（✓）

```
去掉 owner 侧那次 chain 锁（round 194 的实验遗留 ✗）⇒ 实测**未改变** ret=0 ✗ ⇒ 但它确实不该在（已清 ✓）；
⇒ 顺带确认：`requeue-probe` 的调用者仍持有 target ✓（errno=0 ✓）。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
