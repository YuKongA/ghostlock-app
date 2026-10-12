# 43499 · requeue：`errno` 11 → 22 之后的下一步实验（2026-10-12）

## 一、已有信号（真机逐字）

```
第 250 轮改动（park 与 uaddr 同改 wait_word）⇒ main: requeue ret=-1 errno=**22** (EINVAL) × 32
此前（park/uaddr = chain_word）      ⇒ main: requeue ret=-1 errno=**11** (EAGAIN) × 15
main: waiter lock ret=0 errno=0 × 32   ⇒ 锁一直成功
⇒ 结论：改动**触到了内核路径**（回答变了），而且**方向**是对的：内核现在看得到那个字。
```

## 二、内核契约与旧码（逐字）

```
FUTEX_CMP_REQUEUE_PI(uaddr, val, uaddr2, val3)：等待者**停在 uaddr** 且**持有 uaddr2**。
旧码 641aaf6：wait_requeue_pi(**chain**, **target**) ⇒ 等 chain、持 target。
⇒ 本树现状：waiter 持 wait_word（:505），park/uaddr 现改到 wait_word ⇒ uaddr2=target_word **无人持有**
   ⇒ ⇒ EINVAL（22）正是"uaddr2 不合法/未被持有"的典型。
```

## 三、下一步实验（两处，一起改才有意义）

```
① **回退第 250 轮的两处**（park 与 uaddr 回到 chain_word）；
② **让 waiter 持 target_word**（uaddr2），但**必须在 owner 释放之后**才去持 —— 否则二者争锁，
   就是第 203 轮删掉它的那个卡死（旧码 mr_x 是 LOCK(l1) ⇒ LOCK(l2) ⇒ UNLOCK(l1) ⇒ UNLOCK(l2)，
   即**短暂**持 l1 后释放）。
⇒ ⇒ 实现要点：owner 侧在它那次 lock/unlock 序列之后置一个"l1 已释放"的标志；waiter 等该标志再 lock_pi(target_word)；
   随后 park 在 chain_word 上；main 的 requeue 用 (uaddr=chain_word, uaddr2=target_word)。
判据：真机 main: requeue ret=-1 errno=**35**（EDEADLK，旧码成功形态）⇒ 再看 main: victim uid=0。
```

## 四、若不成立（下一个探针）

```
若仍 EINVAL/EAGAIN ⇒ 逐项核对内核要求的四个字与它们的**持有关系**，并对照旧码 mr_x / mr_y / mr_cond
的实际持有时序（fops.c 是冻结参照，可逐行读）。
```
