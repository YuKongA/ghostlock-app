# 43499 · 最后一处语义对齐生效（round 100）：requeue 与归档成功同形

## 改动（与退役逐行同形 ✓）

```c
// 退役 race/threads.cpp：先 lock_pi(chain)，再 wait_requeue_pi(chain, target)，requeue 也从 chain 搬
waiter:  Ops::wait_requeue_pi_until(&shared_.chain_word, &shared_.target_word, park_us);   ← 原来在 wait_word ✗
requeue: Ops::cmp_requeue_pi(&shared_.chain_word, &shared_.target_word);                   ← 原来在 wait_word ✗
```

## 真机（逐字）

```
TEMPREQ after ret=-1 errno=11     ← EAGAIN：与归档成功的 ret=-1 errno=35(EAGAIN) 同形 ✓
                                     （此前是 ret=0 = 一个都没看到 ✗）
TEMPWAIT waiter ret=-1 errno=1    ← EPERM ✓
main: route status=0/0 attempts=2 calls=1 successes=1
TEMPBISECT got=65536 at2000=0x0  ← 写仍未落 ✗
main: victim uid=2000
```

## 累计修齐的 14 处（全部以退役代码为据、带注释）

1 owner_started ✓ 2 挑页奇偶 ✓ 3 载荷映射候选 ✓ 4 nr_requeue ✓ 5 val3 ✓ 6 引擎契约 ✓
7 三个 task 锚 ✓ 8 凭据路径接线 ✓ 9 凭据别名空间 ✓ 10 页面进内核 ✓ 11 stamp 指针少 delta ✓
12 delta 有符号 ✓ 13 分块铺页面 ✓ 14 **park/requeue 的字（本轮 ✓）**

## 仍未达成的与下一步

- `uid=0`：**从未** ✗；写落地：**未达成** ✗；
- 下一个未对照项：触发每轮的**warm-up**语义（退役 `arm_sequence` 之前只复制、不 arm ✓ 且"the kernel's
  own shape check refuses the copy on most of them" ✓）与**拷贝落在被释放的 waiter 上**这个前提本身 ✓；
- 判据不变：`TEMPBISECT at2000` 非零 ✓ 或 `victim uid=0` ✓。
