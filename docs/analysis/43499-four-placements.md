# 43499 · 触发位置的四种组合（round 98-99）与一处未对齐的语义

## 四种组合的真机结果（全部"健康"，全部无写入）

| # | 触发线程 | 触发时机 | park | 拷贝 | consumer | 写入 | 状态 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 等待者 | park **中**（上轮） | EPERM | — | 0 | ✗ | `2/61` |
| 2 | 调用线程 | park 中 | EPERM | — | 0 | ✗ | `2/61` |
| 3 | 调用线程 | park 中（修完页面后 ✓） | EPERM | 落地 ✓ | 1 ✓ | ✗ | `0/0` ✓ |
| 4 | **等待者** | park **后**（本轮，与退役"落在调用者栈上"一致 ✓） | EPERM | 落地 ✓ | 1 ✓ | ✗ | `0/0` ✓ |

⇒ 结论：**触发位置已不是判别项** ✗（四种都健康或已修好 ✓），而**写仍不落** ✗。

## 旧代码又指出一处**未对齐的语义**（下一步）

```c
// 退役 race/threads.cpp
static int wait_requeue_pi(uint32_t *chain, uint32_t *target)   // ◀ 在 **chain** 上 park
static int cmp_requeue_pi(uint32_t *chain, uint32_t *target)    // ◀ 从 **chain** 搬走
```

⇒ 退役是"**park 在自己持有的那个字上**"（waiter 先 `lock_pi(chain)` ✓ 再在 `chain` 上 park ✓）；
  而本引擎的等待者 `lock_pi(&chain_word)` ✓ 却 park 在 **`wait_word`** ✗、requeue 也从 `wait_word` 搬 ✗。
  ⇒ 两条虽然自洽 ✓，但与退役**不是同一个字** ✗ ⇒ 下一步：把 park/requeue 的字与**持有的那个字**对齐 ✓。

## 判据

- `TEMPBISECT at2000` 变为非零 ✓ 或 `victim uid=0` ✓。
