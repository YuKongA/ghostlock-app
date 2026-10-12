# 43499 · 两条 route 的唯一已知差异：消费者的 setattr（round 194）✓

## 本轮读数（HIT2/HIT4 ✓）

```
main: select words wait=0x0 chain=0x0 target=0x0    ← 三个字**终值为 0** ✓ ⇒ 锁都正常释放 ✓
parkerrno=110（ETIMEDOUT）✓ —— 与归档退役**同一读数** ✓（它容忍 ✓）
setattr=-1 ✗（消费者的 sched_setattr 被拒 ✓）
```

## 与 multicast 的对照（关键 ✓）

| 项 | multicast（本晚日志） | select（本轮） |
| --- | --- | --- |
| 消费者的动作 | `successes=1` ✓（`setattr` 成功 ✓） | `setattr=-1` ✗ |
| park | `requeue=-1 errno=35`（EAGAIN ✓，容忍 ✓） | `parkerrno=110`（ETIMEDOUT ✓，容忍 ✓） |
| 三个字 | — | 终值全 0 ✓ |
| 写落地（enforce） | 1 ✗ | 1 ✗ |

⇒ 两支唯一**已知**的行为差异就是消费者的动作：一条成功 ✓、一条被拒 ✗。
⇒ 而退役的判据正是"`setattr` 成功 ⇒ 记一次 success" ✓ ⇒ ⇒ 下一批就从这里入手 ✓。

## 下一步（下一批，最省力 ✓）

1. 把 consumer 的 **errno** 也记出来 ✓（现在是 `-1` 无 errno ✗ ⇒ 与 park 同样处理 ✓）；
2. 按 errno 定修法：`EINVAL` ⇒ 策略字段 ✗ / `EPERM` ⇒ 目标线程状态 ✗ / `ESRCH` ⇒ tid ✗；
3. 判据仍是 `enforce` 1→0 ✓（`successes` 只作旁证 ✓）。

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
