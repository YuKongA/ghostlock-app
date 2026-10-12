# 43499 · `select_stack` 机制缺口：三个字 + 等待者要用 WAIT_REQUEUE_PI（round 189）✓

## 快照的三演员（逐行 ✓）

```cpp
// 快照：PiRace { wait_futex, target_futex, chain_futex }   ← **三个**字 ✗（我只用了两个 ✗）
// waiter（:164-178 ✓）：
FUTEX_LOCK_PI(chain_futex)                                    // 先拿 chain ✓
FUTEX_WAIT_REQUEUE_PI(wait_futex, timeout, target_futex, 0)    // ★ park 在 wait_futex，请求被搬去 target ✓
FUTEX_UNLOCK_PI(chain_futex)
// owner（:184-198 ✓）：
FUTEX_LOCK_PI(target_futex)                                    // 先拿 target ✓
FUTEX_LOCK_PI(chain_futex)                                     // ★ 再去拿 chain ⇒ 这就是"链" ✓
FUTEX_UNLOCK_PI(target_futex)
// consumer（:225 ✓）：sched_setattr(waiter_tid, nice) / FUTEX_LOCK_PI(target,50ms) 兜底 ✓
```

## 我此前的两处结构错（✗，都由本轮读数逼出 ✓）

| # | 我的做法 | 应为 | 后果 |
| --- | --- | --- | --- |
| 1 | 只传 `word(0)`/`spare(0)` 两个字 | **三个**：wait / target / chain ✓ | 没有 wait 字 ⇒ 等待者无处 park ✓ |
| 2 | waiter 只 `lock_pi(chain)` 后进 `pselect` | 必须 **`WAIT_REQUEUE_PI(wait → target)`** park ✓ | `cmp_requeue_pi` 无等待者可搬 ⇒ 恒 0 ⇒ `successes=0` ✗ |

⇒ 这解释了观测：128 轮全 Refused、`successes=0` ✓（不是词表/时序问题 ✗，是**等待者根本没在链上 park** ✗）。

## 下一步（下一批 ✓）

1. 配置里补**第三个字**（`wait_word` ✓，由文档/区域给 ✓）；
2. `waiter_entry` 改成：`lock_pi(chain)` ⇒ 置 ready ⇒ **`WAIT_REQUEUE_PI(wait, …, target)`** ⇒ `unlock_pi(chain)` ✓；
3. `pselect` 的位置再核（快照里它是**拷贝载体** ✓，与 futex park 的关系要按快照读清 ✓）；
4. 判据不变：`successes` 由 0 变 ≥1 ✓ ⇒ 再看 `enforce` 1→0 ✓ ⇒ 再看 `uid=0` ✓。
