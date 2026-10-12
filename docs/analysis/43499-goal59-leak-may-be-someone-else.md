# 43499 · 第 59 轮：`cred` 偏移自洽 ⇒ 嫌疑改为"`spawned.task` 是不是受害者本人" ✓✓

## 一、交叉验证（✓✓，夹具内自洽 + 与 5.15 布局吻合 ✓）

```
abi.task_struct（按值排序 ✓）：prio 0x7c ✓ / normal_prio 0x84 ✓ / sched_task_group 0x400 ✓ /
  tasks 0x4d0 ✓ / atomic_flags 0x598 ✓ / pid 0x5d8 ✓ / tgid 0x5dc ✓ /
  **real_cred 0x790 ✓** / **cred 0x798 ✓** / comm 0x7a8 ✓ / seccomp 0x860 ✓ /
  pi_lock 0x884 ✓ / pi_waiters 0x898 ✓ / pi_top_task 0x8a8 ✓ / pi_blocked_on 0x8b0 ✓；
payload 里的 task_* 与之一致 ✓（prio 0x7c ✓ / group 0x400 ✓ / pi_lock 0x884 ✓ …）⇒ ⇒ **内部自洽** ✓；
且逐条与内核 5.15 的常见布局吻合 ✓（real_cred/cred 相邻 ✓、pid/tgid 相邻 ✓、comm 紧随 cred ✓）。
⇒ ⇒ **⇒ 「cred 偏移错」这条嫌疑基本排除** ✗ ✓。
```

## 二、于是嫌疑转向（✓✓）

```
若"目标地址"也对 ✓ ⇒ 那么剩下的解释只有：**`spawned.task` 泄漏到的不是受害者本人** ✗；
⇒ 即：perf/泄漏给出的是一个**别的** task（同在内核窗内 ✓ ⇒ 地址看着完全正常 ✓）；
⇒ 于是我们"把 init_cred 写进了**别人的** cred" ✓ ⇒ 效果：**受害者不变** ✓✓；
⇒ ⇒ **这一条与全部读数吻合** ✓（写值 ✓ 目标同窗 ✓ clean=1/1 ✓ 而 uid 恒 2000 ✓）。
```

## 三、下一步（✓，两条都可查 ✓）

```
① **判"泄漏到的是谁"**：退役 `spawn_victim` 的 perf 泄漏读的是**子进程自己**的 task ✓（⇒ 子侧 perf ✓）；
   本树是否同形 ✗ ⇒ 读 `victim_process.cpp` 的泄漏段 ✓（已有 ✓）；
② **间接验证**：若泄漏到别的 task ⇒ 其地址在**多次重试**间应呈现"多个不同 task" ✓，
   而受害者每次都是**新 fork 的** ✓ ⇒ 可对比"泄漏值是否随子进程变化" ✓；
③ 判据：任一轮 `main: victim uid=0` ✓。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
