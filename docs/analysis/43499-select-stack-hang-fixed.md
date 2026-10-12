# 43499 · `select_stack` 卡住已根因定位并修复（round 188）✓

## 根因（与上一轮的嫌疑 #1 一致 ✓）

```
1. waiter 线程第一件事是 `FUTEX_LOCK_PI(chain_word)` ✓；
2. 而我传给它的 `words_region.word(0)` 里可能**残留非零值** ✗（该区此前被用过 ✓）；
3. `FUTEX_LOCK_PI` 遇到非零字 ⇒ 认为有人持有 ⇒ **无限等待** ✗（PI 锁没有超时 ✓）；
4. 于是 waiter 永不就绪 ⇒ `stop_actors` 的自旋永不退出 ⇒ 主线程卡死 ✓（真机 180 s 被杀 ✓）。
```

## 修复（两处，都是"把上限加上" ✓）

| 位置 | 改动 |
| --- | --- |
| `main.cpp` 的 select 分支 | **两个字先归零** ✓（`*word = 0; *spare = 0;` ✓）—— 直接消除根因 ✓ |
| `stop_actors` | 自旋加**迭代上限**（40 万次 ✓）⇒ 超限则**不 join 改 detach** ✓（`pthread_join` 无超时 ✗）⇒ 并记 `status_step = 90` 以便观测 ✓ |

## 真机（修复后 ✓）

```
INNER_RC = **9** ✓（此前 124 ✗）⇒ 跑完 ✓
main: select route status=0 step=61 attempts=128 calls=128 successes=0 refusal=select-stack-no-consumer-success
⇒ **128 轮全部走完** ✓；`successes=0` ✗ 是**另一个**问题（内核为何不搬等待者 ✓），现在可干净测量 ✓
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
