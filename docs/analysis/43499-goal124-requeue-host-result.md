# 43499 · 第 124 轮：requeue 移入 waiter 线程（有据、门禁绿、仍未翻动）✓✗

## 一、改动（✓，两处 ✓）

```cpp
// (A) waiter_main：park 之后、trigger() 之前 ⇒ 本线程自己做 requeue ✓
parked = Ops::wait_requeue_pi_until(&chain_word, &target_word, park_us);
shared_.waiter_waiting.store(1);
**requeue();**                       // ★ 新增（第 124 轮 ✓）⇒ 与退役"park 与 requeue 同线程"同形 ✓
trigger();
// (B) execute()：删除原来的 `requeue();` ✓ ⇒ 避免二次 requeue ✓
⇒ `grep -c 'requeue();'` = **1** ✓（无重复 ✓）。
```

## 二、结果（✓✗，如实 ✓）

```
门禁：`android` RC=0，**0 error / 0 warning** ✓；`check` **109 PASS** ✓；
真机：`main: victim comm after len=15 first=103` × **5** ✗（⇒ 轮数由 13 变为 5 ✓）；
      `main: victim uid=2000` ✗；`main: route status=0/0 clean=1/1 attempts=16` ✓。
⇒ ⇒ **⇒ 候选①（requeue 的宿主线程）**也已排除** ✗（⇒ 对齐之后结果不变 ✓）。
```

## 三、剩余候选（✓，仅剩两条 ✓）

```
① **页的生命周期/送达路径** ✗：本树在 waiter 线程内 `trigger()`（⇒ 页在该线程栈/自己的 fd 上 ✓）；
   退役由 race 线程送页、且**页是先"预建"好的**（`stash_prebuilt_page` ✓）⇒ ⇒ **⇒ 送达者不同** ✗；
② **内核侧对该目标的语义校验**（`_ref`/大小/`pi_blocked_on` 指向 ✓）✗ —— 无源码可查 ⇒ 只能由读数反推 ✓；
⇒ ⇒ **⇒ 可做的一步**：把"送页"也做成**预建＋由另一线程送** ✗（⇒ 与退役同形 ✓）⇒ 即第 25 轮那条的另一半 ✓。
```

## 四、门禁（✓，全绿 ✓）

```
check → **109 PASS** ✓；android → RC=0，**0 error / 0 warning** ✓；lint → 0 ✓
```
