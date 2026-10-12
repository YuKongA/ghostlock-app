# 43499 · 引擎时间线与退役的差异（round 75-76）

## 已证伪（本轮真结果）

- `consumer.delay_us = 200000`（想对齐退役的 `post_requeue_settle_us`）⇒ 稳定失败 ✗
  `status=2/61 attempts=8 calls=0`（61 = start_threads 失败）⇒ **该旋钮在本引擎里语义不同** ✓；
- 回退 `delay_us = 0` ⇒ 恢复 `status=0/0 attempts=2 calls=4` ✓；
- 另一条更正（上轮）：**"只跑了 w1" 是错的** ✗ —— w1/w2/w3 三步都执行且都 won ✓（脚本与队列层是通的 ✓）。

## 引擎的消费者语义（读清 ✓）

```
consumer_main：按 consumer_delay_us 等 → 突发 setattr（改等待者调度属性 = 让内核走 PI 链的动作）
  setattr 被拒 ⇒ 自己 lock_pi_until(target_word, fallback_lock_us) 再 unlock（fallback ✓）
  accepted == 0 ⇒ consumer_success++   ⇒ successes=4 = 4 次成功的 setattr（**不是**写入成功）
```

## 与退役的三处时间线差异（都还在，且都未对齐）

| 退役（.conf） | 本引擎 | 说明 |
| --- | --- | --- |
| `waiter.ready_timeout_ms=10000` | 无（owner 用 yield 死等 ✓） | 语义等价，非缺口 |
| `waiter.post_requeue_settle_us=200000` | 无 ✗ | requeue 之后、consumer 动作之前的相位 |
| `waiter.post_adjust_settle_us=100000` | 无 ✗ | consumer 调整之后的相位 |

⇒ 缺口不是"三个数"，而是**两个相位**：需要在引擎里（不是配置里）插：

1. `requeue()` 返回之后、置 `consumer_go` 之前：等 `post_requeue_settle_us`；
2. 一次 `setattr` 之后、下一次之前：等 `post_adjust_settle_us`。
值仍走 wire（键沿用退役名：`execution.race.post_requeue_settle_us` / `post_adjust_settle_us`），schema 同批加。

## 账

- 拿 uid 0：**从未** ✗；写落地：未达成 ✗；
- 已修七处移植缺口 + 凭据接线 + 线性映射别名 + 三个 task 锚（均带注释 ✓）；
- 已证对：页面字节、元组、prio、list_link、stamp 落地、三步执行、脚本顺序；
- 门禁：android RC=0 ✓、check RC=0/108 PASS ✓、lint 0 ✓。
