# 43499 · 地址同字 ⇒ 排除 #9；剩"入队时 owner 未持 target"（round 225）✓✓

## 本轮读数（✓）

```
waiter : wait=0x7bd6630004 ✓  target=0x7bd6630000 ✓
requeue: wait=0x7bd6630004 ✓  chain=0x7bd6628000 ✓  target=0x7bd6630000 ✓
⇒ 两侧**同一地址** ✓（4 轮一致 ✓）⇒ 排除"不是同一个字" ✓。
```

## 排除表（本晚累计 ✓，九项）

| # | 已排除 | 判据 |
| --- | --- | --- |
| 1 | 时机竞态 ✗ | 50 ms 沉降下 ret 仍 0 ✓ |
| 2 | errno 陈旧 ✗ | 调用前清零后读数不变 ✓ |
| 3 | stdout 的 fd 借用致卡 ✗ | 低位 fd 改 {3,4,5} ⇒ 不再卡 ✓ |
| 4 | 每轮未归零 ✗ | 已改每轮归零 ⇒ 路线跑完 4 轮 ✓ |
| 5 | 引擎未武装 ✗ | 已 `race_.prepare(wait,target)` ✓ |
| 6 | 默认 landing 是空实现 ✗ | 已绑真 landing ✓（`cmp_requeue_pi` 真 syscall ✓） |
| 7 | owner 侧多余 chain 锁 ✗ | 已清 ✓（未改变读数 ✓） |
| 8 | 参数形状不同 ✗ | 与快照**逐字一致** ✓ |
| 9 | 两侧不是同一个字 ✗ | 地址逐字相同 ✓（本轮 ✓） |

## 剩下的唯一候选（✓，且时间线直接支持 ✓）

```
内核侧：`FUTEX_WAIT_REQUEUE_PI` 的入队语义与 `uaddr2`（target）的持锁状态相关 ✓；
而我的时间线是：`waiter pre-park` ✓ → **之后** owner 才 `lock_pi(target)`（在 `race_.run` 内 ✓）；
⇒ ⇒ 下一批实验：**让 owner 在等待者 park 之前就持有 target** ✓；
   做法（安全 ✓）：主线程在 `run_actors` **之前**先 `lock_pi(target_word_)` ✓
   （引擎随后会**再锁一次** —— PI 锁对**同一线程**是可重入的 ✓ ⇒ 不会自锁 ✓）。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
