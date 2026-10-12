# 43499 · `select_stack` 真机**卡住**（round 187）：硬事实与嫌疑清单

## 硬事实（本轮 ✓）

```
单跑：INNER_RC=124（被 180 s 上限杀掉 ✗）  ELAPSED=180s  输出仅 1 行 ✓
⇒ 不是"慢" ✗，是**卡** ✓；且 canonical（multicast）那条跑每次都正常结束 ✓
⇒ 所以卡的相位在**本 route 新增的部分**（两枚演员 + 有界 pselect + join ✓）。
```

## 嫌疑（按可能性排 ✓，都有代码依据 ✓）

| # | 嫌疑 | 依据 | 何以会卡 |
| --- | --- | --- | --- |
| 1 | waiter 在 `lock_pi(chain_word_)` 上阻塞 | `waiter_entry` 第一件事 ✓ | 若 chain 字**已被 PI 锁持有**（例如同一字被另一处 ✗）⇒ 阻塞无超时 ✗ |
| 2 | `stop_actors` 的 `while (waiter_done_ == 0) yield;` | `stop_actors` ✓ | 若 waiter 永不到 `pselect`（如在 1 处卡住 ✗）⇒ 该循环**无限自旋** ✗ |
| 3 | `pthread_join` 无超时 | `stop_actors` ✓ | 被 join 的线程不返回 ⇒ join 永等 ✗ |
| 4 | `race_.run` 里 `lock_pi(target_word_)` 与 consumer 兜底争用 | 两侧都取 target ✓ | 争用会**返回**，一般不致卡 ✗（可能性最低 ✓） |

## 下一步（下一批，按序 ✓）

1. **给三处等待都加上限**（并列打印相位标记 ✓）：`lock_pi` 改 `*_往` 可试锁变体或加超时 ✓、
   `stop_actors` 的自旋加**迭代上限** ✓、`join` 前先确认 `waiter_done_` ✓；
2. 加**相位标记**（进入/离开 `run_actors`、`race_.run`、`stop_actors` ✓）⇒ 一次跑即可定位 ✓；
3. 定位后再谈 `successes`（判据不变 ✓：0 → ≥1 ✓）。

## 影响面（如实 ✓）

- 该卡**只影响 `select_stack` 这条新 route** ✓（canonical 与 43284 两条路照常 ✓）；
- 门禁仍全绿 ✓（卡是真机行为，不是构建问题 ✓）。
