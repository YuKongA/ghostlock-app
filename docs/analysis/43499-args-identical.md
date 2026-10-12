# 43499 · 参数逐字一致 ⇒ 排除 a/c；剩内核入队语义（round 224）✓

## 逐字核对（✓，快照 vs 我）

```
快照 WAIT_REQUEUE_PI :174-175  futex_op(&wait_futex, FUTEX_WAIT_REQUEUE_PI, 0, &timeout, &target_futex, 0)
我的   WAIT_REQUEUE_PI            syscall(SYS_futex, chain, FUTEX_WAIT_REQUEUE_PI, 0, &ts, target, 0)
                                              ⇒ val=0 ✓ / 超时结构 ✓ / uaddr2=target ✓ / val3=0 ✓ ⇒ **一致** ✓

快照 CMP_REQUEUE_PI  :243-245     futex_op(&wait_futex, FUTEX_CMP_REQUEUE_PI, 1, (void *)1, &target_futex, 0)
我的   CMP_REQUEUE_PI             syscall(SYS_futex, chain, FUTEX_CMP_REQUEUE_PI, 1, 1, target, 0)
                                              ⇒ val=1 ✓ / val2=1 ✓ / uaddr2=target ✓ / val3=0 ✓ ⇒ **一致** ✓
```

## 于是候选表收缩到两项（✓）

| # | 候选 | 现状 |
| --- | --- | --- |
| 1 | `wait_word_` 与 requeue 的 `uaddr1` **不是同一个字** ✗ | 我需要**打印地址**（现在只打了值 ✓）⇒ 一行即可排 ✓ |
| 2 | 等待者**入队时**内核判定的形态与 requeue 期望不符 ✗ | 需要更细的内核侧证据（难 ✗）⇒ 先把 1 排掉 ✓ |

## 下一批（一行 ✓）

```
打印 `wait_word_` / `chain_word_` / `target_word_` 的**地址**（%p ✓）于两侧（waiter 与 requeue ✓）⇒
若地址相同 ✗ ⇒ 排 1 ✓ ⇒ 再谈 2 ✓。
```

## 顺带（✓）

```
本晚已排除（都有读数 ✓）：时机竞态 ✗（50 ms 沉降 ✓）、errno 陈旧 ✗（清零后不变 ✓）、
stdout 的 fd 借用 ✗（改到 3/4/5 ✓）、每轮未归零 ✗（已修 ✓）、engine 未武装 ✗（已修 ✓）、
默认空 seam ✗（已绑真 seam ✓）、owner 侧多余的 chain 锁 ✗（已清 ✓）。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
