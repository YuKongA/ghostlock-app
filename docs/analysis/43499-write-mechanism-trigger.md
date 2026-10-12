# 43499 · 写机制与触发条件读清（round 202）✓

## 写机制（快照头部 + 触发点 ✓）

```
快照 :6 ：「…触发 **PI 链 walk 完成擦除式写入**」✓
⇒ 写**不是**我们的代码做的 ✓ —— 是内核在内核态沿着 PI 链 walk 时"擦除式"落下的 ✓；
⇒ 所以整条链的要害是：让内核**真的 walk 一个我们控制的陈旧 waiter** ✓。
```

## 触发点（:242-245 ✓）

```cpp
while (!race.waiter_waiting.load() || !race.owner_started.load()) ::usleep(100);  // ★ 两个标志
::usleep(1000);
futex_op(&race.wait_futex, FUTEX_CMP_REQUEUE_PI, 1, (void *)1, &race.target_futex, 0);
//        ↑ 在 wait 上        ↑ op        ↑val2=1      ↑val3=0
```

## 与我的实现的差异（三处 ✓，都由本页行号支撑 ✓）

| # | 快照 | 我 |
| --- | --- | --- |
| 1 | 等 **`waiter_waiting`**（park 后置位 ✓） | 只等 `waiter_ready_`（park **前**置位 ✗） |
| 2 | 等 **`owner_started`**（owner 拿了 target 之后 ✓） | 没有这个同步点 ✗ |
| 3 | `val3 = 0` ✓ | `PiRace` 的 val3 由内部给（此前记录为"另一字的当前值" ✗ ⇒ 需核 ✓） |

⇒ ⇒ 这三处合起来等于："**在内核开始 walk 之前，把该在的东西都摆好**" ✓ ——
    我此前是"起完演员就 pselect" ✗ ⇒ 触发可能早于等待者真正 park ✓。

## 下一步（下一批 ✓）

1. waiter 侧补 **`waiter_waiting`**（在进入 park 之后置位 ✓，或在 park 前紧邻 ✓，按快照 :171-175 的形状 ✓）；
2. 主线程在触发前**等两个标志** ✓（`waiter_waiting` + `owner_started` ✓）；
3. 核对 `PiRace` 的 `val2/val3` 是否 = 1/0 ✓（若不同 ⇒ 改为与快照一致 ✓）；
4. 判据仍是 `enforce` 1→0 ✓。

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
