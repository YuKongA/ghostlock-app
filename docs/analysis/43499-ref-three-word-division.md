# 43499 · requeue：ref 给出的三字分工（2026-10-12，用户指示「先参考 ref」）

## 一、ref 的权威做法（ghostlock-app-ref/src/core/race/threads.cpp:20-72 逐字）

```
futex_op(&race->chain_futex,  FUTEX_LOCK_PI, ...)                       ⇒ 等待者**持有** chain_futex
race->waiter_waiting.store(1);                                         ⇒ **在 park 之前**（本树第 240 轮已一致）
futex_op(&race->wait_futex,   FUTEX_WAIT_REQUEUE_PI, 0, &timeout, &race->target_futex, 0)
                                                                       ⇒ **停在** wait_futex、目标 target_futex
… controller.execute(request) … ghost disarm … route_done.store(1)
futex_op(&race->chain_futex,  FUTEX_UNLOCK_PI, ...)                     ⇒ 收尾释放它持有的那个
⇒ 三字分工：chain_futex ＝ 等待者**持有** ／ wait_futex ＝ 等待者**停驻** ／ target_futex ＝ requeue 的**目标**
```

## 二、本树现状与错位（multicast_route.hpp）

```
waiter :514  lock_pi(&shared_.wait_word)      ✗ 该是 chain_word（等待者持有的那个）
waiter :539  wait_requeue_pi_until(&shared_.chain_word, &shared_.target_word, park_us)
                                              ✗ 该停在 wait_word（而目标是 target_word ✓）
waiter :555  unlock_pi(&shared_.chain_word)   ✓ 收尾释放 chain_word（但本树从未 lock 过它 ✗ ⇒ 自证错位）
main   :389  cmp_requeue_pi(&shared_.chain_word, &shared_.target_word)   ✗ 应为 (wait_word, chain_word)
⇒ 对照后：错位在两处（锁的字、停的字），且 requeue 的两个参数**恰好相反**。
```

## 三、待做的改动（两处，互为配套）

```
① waiter：lock_pi(&shared_.**chain_word**)（⇒ 与 :555 的 unlock 配对 ✓）、
           wait_requeue_pi_until(&shared_.**wait_word**, &shared_.target_word, park_us)；
② main  ：cmp_requeue_pi(&shared_.**wait_word**, &shared_.**chain_word**)；
⇒ 判据：真机 main: requeue ret=-1 errno=**35**（EDEADLK，旧码成功形态）⇒ 再看 main: victim uid=0。
```

## 四、用户要求的检查（下一步）

```
「出现任何问题先参考 ref，并进行 cmp_disasm 检查」⇒ 本轮已参考 ref（见 §一）；
cmp_disasm（AGENTS §11 ⇒ introduction.md §二）在改动落地后对本树与 ref 的 waiter 路径做逐指令对比，
定位除上述错位之外的任何差异（ref 的 plan 文档曾记 waiter_thread「归一化后逐条相同」）。
```
