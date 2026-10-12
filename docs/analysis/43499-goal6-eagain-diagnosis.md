# 43499 · 新目标第 6 轮：第二耗尽 = EAGAIN（进程/线程上限）✓✓

## 一、诊断读数（✓，本轮新增一行诊断打印 ✓）

```
`main: victim fork failed errno=**11**` ✓ ⇒ **EAGAIN**（进程/线程上限 ✓）—— 不是 ENOMEM（12 ✗）；
轮数：`step retry w2 round=14 / 15` ✓ ⇒ 循环本身跑得动 ✓，死在 spawn ✓。
```

## 二、已排除（✓，逐条有出处 ✓）

| 候选 | 结论 | 依据 |
| --- | --- | --- |
| 僵尸进程 | **排除** ✗ | `victim_context.cpp:21`：`retire_child()` = `kill(-pid)` + `kill(pid)` + **`waitpid` 循环** ✓ |
| 路由线程未 join | **排除** ✗ | `multicast_route.hpp:354/358/362` `pthread_join(waiter_/owner_/consumer_)` ✓；select 侧 `stop_actors` ✓ |
| fd 耗尽 | **已修** ✓ | 第 5 轮：`PFOPEN fd=` 512→24813 ✗ ⇒ 修后**恒为 3** ✓ |
| 内存 | **排除** ✗ | errno=11 ≠ 12 ✓ |

## 三、剩余候选（✓，下一步逐条验）

```
① `leak_pid` 子进程（每 prepare 一个 ✓）：`prepare_page.hpp:161-162` 只在**某条收尾路径** `stop_child` ✓
   ⇒ 需确认"成功路径"也停 ✓（否则每轮累积 1 个 ✓ —— 15 轮 = 15 个 ✗，是否够触发 EAGAIN ✗ 待测 ✓）；
② `perf_event_open` 的 ring（内核对象 ✗ 不计进程 ✗，但**是否算"文件/事件"配额** ✓）；
③ uid=2000（shell）的进程数上限本身较低 ✓ ⇒ 需实测"跑到第 N 轮时本进程的子进程数" ✓。
```

## 四、判据与下一步（✓）

```
验收：`main: victim fork failed errno=` **不再出现** ✓ ⇒ 15 轮全部完成 ⇒ 才能读满 15 次的 `victim uid` ✓；
再并行追"写为何不落"（`slab_drain` / `in_direct_map` / `discard_prebuilt_page` 三件 ✓）。
```
