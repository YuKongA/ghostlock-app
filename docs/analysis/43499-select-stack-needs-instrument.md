# 43499 · `select_stack`：三处修正后仍是 successes=0（round 191）✓

## 本轮事实（3 次命中 ✓）

```
HIT1/2/3: main: select route status=0 step=61 attempts=128 calls=128 **successes=0**
          refusal=select-stack-no-consumer-success
跑完就结束（不卡 ✓）；run4 撞奇偶门 ✗
⇒ 三处修正（三字 ✓ / WAIT_REQUEUE_PI ✓ / 武装 ✓）都到位了，内核**仍未搬动等待者** ✗
```

## 新增的一处（快照依据 ✓）与它的读数

```
owner 侧也上链：lock_pi(chain) ⇒ …… ⇒ unlock_pi(chain)   （快照 :194 ✓）
⇒ 但整跑**没有变慢** ✗ ⇒ 说明这次 lock_pi **没有被阻塞** ✓
⇒ ⇒ 也就是：**等待者并没有持有 chain** ✗（否则 owner 会阻塞到它持链为止 ✓）
⇒ ⇒ ⇒ 嫌疑集中到 waiter 线程本身：它的 `lock_pi(chain_word_)` 是否失败/是否真的跑到 ✗
```

## 下一步（下一批，最省力且决定性 ✓）

| # | 动作 | 为什么决定性 |
| --- | --- | --- |
| 1 | 把 **waiter 的 `lock_pi` 返回值**、**`wait_requeue_pi_until` 的返回值**、**consumer 的 setattr 结果** 记进报告（`status_step` 或新字段 ✓） | 现在这些错误码被 `(void)` 丢掉 ✗ ⇒ 无法区分"没跑到/被拒/时刻不对" ✓ |
| 2 | 再按读数决定：修等待者自身 ✓ 还是修时序 ✓ | 判据不变（successes 0 → ≥1 ✓） |

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
