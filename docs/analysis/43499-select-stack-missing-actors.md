# 43499 · `select_stack` 首跑 `successes=0` 的诊断（round 183）✓

## 诊断（读码即得 ✓）

```
PiRace::run 的自述（pi_race.hpp:116-119 ✓）：
  "This side is the owner, and **only the owner**. The waiter is **another thread** that parks on
   the chain asking to be requeued onto the target …"
⇒ 我接线时只给了 owner 侧 ✗ ⇒ 没有线程 park 在 chain 上 ⇒ cmp_requeue_pi 返回 0 ⇒ Refused ✓
⇒ 于是真机 128 轮全 Refused、successes=0 ✓（与观测完全一致 ✓）
```

## 对照：multicast 引擎有三个演员线程 ✓

| 线程 | 位置 | 职责 |
| --- | --- | --- |
| consumer | `multicast_route.hpp:305` / `consumer_main:503` ✓ | 窗口内做 `sched_setattr`（被拒则 `FUTEX_LOCK_PI` 兜底 ✓） |
| owner | `:310` / `owner_main:480` ✓ | 取 target 字（`lock_pi` ✓） |
| waiter | `:318` / `waiter_main:449` ✓ | **park 在 chain 上**（这才是 `cmp_requeue_pi` 的能量来源 ✓） |

⇒ `select_stack` 要能"搬动等待者"，**必须**补上 waiter（与 consumer）✓；
⇒ 本树既有件里 `PiRace` 只覆盖 owner ⇒ 另外两枚需要从 multicast 引擎**移植**（或抽出共用 ✓）
   ⇒ 约 120 行 ✓，机械 ✓，且判据已硬（`Running` ⟺ 搬动过 ✓）。

## 判据（下一批 ✓）

```
① 真机跑 `wire-43499-select-stack.framed`（**多次统计** ✓，奇偶门在前 ✗）；
② 看 `main: select route … successes=` ⇒ 从 0 变 ≥1 ✓ ⇒ 说明内核搬动了等待者 ✓；
③ 再看 `main: selinux enforce` 1→0 ✓（机制级）；
④ 最后看 `main: victim uid=0` ✓（目标级）。
```

## 门禁（本轮未改码 ✓，仍全绿 ✓）

```
check → RC=0，108 PASS ✓；android → RC=0，0 警 ✓；lint → 0 ✓
```
