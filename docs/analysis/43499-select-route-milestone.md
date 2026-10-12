# 43499 · 里程碑：select 路线首个完整报告（round 218）✓✓✓

## 本轮修掉的第二处"真卡"（✓）

```
读数：尝试1 `words-in wait=0x0 chain=0x508b target=0x0` ✓ → `words-out … chain=0x508b` ✗；
     尝试2 `sel-exec attempt ✓` → EXIT=124 ✗ ⇒ **卡在尝试 2 的 `lock_pi(chain)`** ✓；
成因：每轮结束时 `chain` 停在某个 tid（README 同族：round 205 修过"入口一次性归零" ✗，
      但没有**每轮**归零 ✗）⇒ waiter 第二轮的 `FUTEX_LOCK_PI` 无限等 ✓；
修法：每轮开始（上一轮演员已 stop ✓）把三个字归零 ✓ ⇒ **实测通过** ✓。
```

## 完整报告（y3 ✓，逐字 ✓）

```
main: select route status=0 step=61 attempts=4 calls=4 **successes=0**
      lock=0 ✓  park=-1 parkerrno=110 ✓（与归档退役同读数 ✓）  setattr=0 setterrno=0 ✓（消费者健康 ✓）
      refusal=select-stack-no-consumer-success
main: selinux enforce=1 ✗
main: select words wait=0x0 chain=0x0 target=0x0 ✓（终值干净 ✓）
```

## 意义（✓）

```
从"select 分支从未被走到" ✗ ⇒ 现在**每轮四阶段全跑完并出报告** ✓；
三个演员都健康 ✓（lock ✓ / setattr ✓），字终值干净 ✓ ⇒ 剩下**唯一**的读数差是 `successes=0` ✗；
而那正是快照的判据：`CMP_REQUEUE_PI` 返回 ≥1 ⟺ 内核**真的搬动了等待者** ✓。
```

## 下一批（✓，按快照的同步条件）

```
快照 :242-245 的同步是：等 **`waiter_waiting`**（park 之后 ✓）**且** **`owner_started`** ⇒ 再 requeue ✓；
我目前只等 `waiter_ready_`（park **之前** ✓）+ sleep 1 ms ✗ ⇒ requeue 很可能**早于**等待者真正 park ✓；
⇒ 下一批：把 `waiter_ready_` 改为"**进入 park 之后**置位" ✓（或补一个 `waiter_waiting` ✓），并等它 ✓。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
