# 43499 · `setterrno=3`（ESRCH）：等待者在消费者动手前就退出了（round 195）✓

## 读数（HIT3 ✓）

```
main: select route … lock=0 ✓ park=-1 parkerrno=110 ✓ setattr=-1 ✗ **setterrno=3 (ESRCH)** ✗
⇒ ESRCH = "no such process" ⇒ 消费者调用 setattr 时，目标 tid **已不存在** ✗
```

## 成因（两处，都有快照为据 ✓）

| # | 我的现状 | 快照/应然 | 后果 |
| --- | --- | --- | --- |
| 1 | waiter 只 park **200 ms** ✗（`actor_wait_us_` ✓） | "**永不就绪的 timerfd** 让 select 一直等，使陈旧 waiter 在整个 consumer 窗口内保持可被 walk" ✓ | 200 ms 一过 waiter 退出 ⇒ 之后每次 `setattr` 都 ESRCH ✗ |
| 2 | 探针存**最后一次** `setattr` 的结果 ✗ | —— | 看不到"首次是否成功" ✓（首次很可能也曾失败 ✓ 或曾成功 ✗） |

## 下一步（下一批，两处都小 ✓）

1. **把 park 拉长**（如 2 s ✓）**并把 attempts 调小**（如 8 ✓）⇒ 单跑时长可控（约 16 s ✓）；
2. 探针改为记录**首次** `setattr` 结果与**尝试次数** ✓（分清"从未成功"与"中途失去目标" ✓）；
3. 判据不变：`enforce` 1→0 ✓（`successes` 仅旁证 ✓）。

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
