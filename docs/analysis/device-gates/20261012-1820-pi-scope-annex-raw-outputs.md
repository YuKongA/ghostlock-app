# 设备门禁附录：守卫对照组原始输出与字段快照留痕（2026-10-12）

主件：20261012-1820-pi-scope-and-ring-removal.md

## 附：守卫对照组原始输出（逐字，全部在 /tmp 副本上跑，树未动）

对照件 old_boundary_guard.py 是把新守卫的台账换回「五函数全体」的旧边界版本。
base 是当前树头文件；A 是 PI 段内插 printf；B 是 PI-OUT 之后插 printf；
C 是删 waiter_main 的 PI-IN 标记；C2 是复制 PI-OUT 标记。

```
case base
  old: WINDOW-BODY-DIRTY: judge PI line 24: ghostlock::support::log( info, judge-outcome, ...); 1; exit=1
  new: check: inside PI, no logging, allocation, stdio or indirect dispatch
       check: trigger, consumer_main, waiter_main, owner_main scanned by span; judge exempt (outside PI)
       exit=0

case A  (PI 段内插 printf)
  old: WINDOW-BODY-DIRTY: waiter_main PI line 37  + judge 行; 2; exit=1
  new: WINDOW-BODY-DIRTY: waiter_main PI line 3; 1; exit=1

case B  (PI-OUT 之后插 printf)  <- 本批最关键：边界确实动了
  old: WINDOW-BODY-DIRTY: waiter_main PI line 93  + judge 行; 2; exit=1
  new: check: inside PI, no logging, allocation, stdio or indirect dispatch
       check: trigger, consumer_main, waiter_main, owner_main scanned by span; judge exempt (outside PI)
       exit=0

case C  (删 waiter_main 的 PI-IN 标记)
  old: 只有 judge 行; 1; exit=1
  new: PI-ANCHOR-MISSING: waiter_main start anchor x0; exit=1

case C2 (复制 PI-OUT 标记)
  old: 只有 judge 行; 1; exit=1
  new: PI-ANCHOR-MISSING: waiter_main end anchor x2; exit=1
```

## 附：字段快照的注释与追认留痕

```
multicast_route.hpp:166-172 写清了为什么必须快照：两个 futex 字是活的（owner 取锁后 target_word 被改写、
解锁回 0）=> 出 PI 再读读到的是别的时候的值；撤环前 POD 环把记录写在 park 点 => 天然快照；
首轮漏快照 => 真机该列整列变 0，而基线是 0x1768... 递增 => 快照是这条读数语义的前提。
并注明第 6、7 个字段（park_wait_word / park_target_word）由 Lead 于 2026-10-12 追认。
本批改后与改前产物同字节（5565952）=> 最后一次改动仅注释。
```
