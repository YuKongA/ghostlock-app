# 43499 · 新目标第 8 轮：两个耗尽全部修好（fd ✓ + 僵尸 ✓）✓✓✓

## 一、本轮修的两处（✓）

```cpp
① 成功路径也停子进程（方案 A ✓，prepare_page.hpp 成功尾部 ✓）：
   四组 pid 原先只活在栈上 ⇒ 返回即丢 ⇒ 调用方结构上无法回收 ✗ ⇒ 加 `kill_children(state)` ✓；
   真机确认**不破坏路由** ✓（`route status=0/0 attempts=16 calls=1 successes=1` 与修前一致 ✓）。
② `stop_child` 只杀不收割（heap_ops_linux.hpp ✓）：
   `kill(SIGKILL)` 后无 `waitpid` ✗ ⇒ **僵尸**累积 ⇒ 同样占进程表 ⇒ EAGAIN（errno=11 ✓）；
   已改为 kill + `waitpid`（EINTR 重试 ✓，与 `VictimContext::retire_child()` 同形 ✓）+ 补 `#include <sys/wait.h>` ✓。
```

## 二、真机判据（✓✓）

```
`main: victim fork failed errno=` ⇒ **消失** ✓✓；`step retry w2 round=14 / 15` ⇒ **15 轮跑满** ✓；
`step result w1/w2/w3 => won` 三条齐 ✓ ⇒ **整链跑通** ✓；
但仍 `main: victim uid=2000` ✗ ⇒ 唯一剩下的就是**写本身不落地** ✓。
（注：`won` = 路由成功 ✓，**不等于** root ✗ —— 判据仍是 `victim uid=0` ✓。）
```

## 三、门禁（✓）

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```

## 四、下一步（✓，只剩一条线 ✓）

```
追"写为何不落"（⇒ 现在重试已能跑满 15 轮 ✓，观察条件终于齐了 ✓）：
① 退役 `retry_write_stage` 里我尚未实现的三件：`slab_drain`（每阶段首次 ✓）/ `in_direct_map(target)` ✓
   （目标须在直映射区 ✓）/ `discard_prebuilt_page`（路由失败即丢预建页 ✓）；
② 以及 W2 的 `w2_fast_repair_prebuild/activate` 的**时序**（我只有"修复轮"，未确认"预建在路由前 ✓"）✓；
③ 判据：任一轮出现 `main: victim uid=0` ✓ ⇒ 之后才谈 w3 的 seccomp 与收尾/handoff ✓。
```
