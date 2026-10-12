# 设备门禁：PI 作用域改造 + 撤环（2026-10-12）

## 目的

用户裁定：「所谓窗口体不需要保证无syscall，真正要保证的是PI内无」——禁令作用域由"窗口体函数"
收窄为"PI 内"，并把判据数字改由 shared_ 字段承载、统一在 PI 之后打印。

## 形状与判据

```
禁令文本（race/pi_window.hpp:12-35）改为 inside PI，并逐 actor 写明起止锚点：
  waiter_main   :599（park 前最后一次同步）-> :637 + :647-658（改挂 + 摘除完成）
  owner_main    :666（lock target）-> :690（末次 unlock）
  consumer_main :698 -> :720 + :725-726
  trigger()     全体（唯一调用点 :635 在 PI 段内）
  judge()       整函数在 PI 之外（唯一调用点 :274 在 stop_threads 之后）=> 具名豁免
五条禁令名字与行数未动（避开 static_assert :121 与 a2_test:84），只改 why 文案。

撤环：删 support/window_trace.hpp、support/window_trace_report.hpp、tools/window_io_to_ring.py（惰性死件）；
      删 5 个写点与 flush 调用；PI 内改为就地落字段；新增 report_pi_readings（multicast_route.hpp:492，:295 调用）。
字段：lock_result / lock_errno / park_errno / disarm_result / disarm_errno
      + park_wait_word / park_target_word（后者为必须的快照，见下）

守卫：tools/window_body_guard.py 由"五函数"改为 PI-span 台账（四条 (函数, 起点锚, 终点锚)），
      锚点必须唯一，缺失或重复 => PI-ANCHOR-MISSING 退出 1；judge 为具名豁免。
```

## 守卫可失败判据（副本上实测，树未动）

```
base 现状头文件：旧边界守卫 exit=1（WINDOW-BODY-DIRTY: judge ... log(...)），新守卫 exit=0   => 边界确实变了
A    PI 内插 printf        => 新守卫 exit=1，逐字 WINDOW-BODY-DIRTY: waiter_main PI line 3: std::printf("A in-PI probe\n");
B    PI-OUT 之后插 printf   => 新守卫 exit=0（旧边界守卫 exit=1）                            => 本批最关键证据
C    删 waiter_main PI-IN   => 新守卫 exit=1，PI-ANCHOR-MISSING: waiter_main start anchor x0
C2   复制 PI-OUT 标记       => 新守卫 exit=1，PI-ANCHOR-MISSING: waiter_main end anchor x2   => 锚点唯一性

偏差说明：原稿 C 写"改写 :599 的 store(1)"；实测该语句在 PI 段内还有一份被注释保留的旧码（:645），
          按该文本判锚会把已改掉的活语句误判为仍在 => 锚点改用 PI-IN/PI-OUT 显式标记（每函数唯一）。
          第一次实现时守卫确在 store(1) 上报过 start anchor x2，即其严格性的第一手证据。
```

## 门禁（最终树）

```
make -C core -j1 check                      EXIT=0   PASS=109   FAIL=0
  含 check: inside PI, no logging, allocation, stdio or indirect dispatch（Makefile echo 与守卫各一行）
make -C core -B android NDK_TOOLCHAIN=       EXIT=0   error=0   warning=0   产物 5565952 B
./gradlew markdownlint                       EXIT=0   602 files  0 issues
```

## 真机读数（adb -s QV770MFGJ1 · 同一构建 · 两次冷机 adb reboot 后 sys.boot_completed=1）

```
推前已 make -C core -B android；夹具 core/fixtures/wire-43499-lua-steps.framed（与设备同）

项                                 run A(pid 5784)  run B(pid 5712)  判读
rc                                 9                9                基线
main: refused (param-invalid)      0                0                基线
main: step result w1 => won        1                1                基线
value=9（设备日志）                 1                1                基线
main: victim uid=2000              14               16               == 各自 stage 数，逐 stage 一条
access bound route=... seam=1      14               16               门面接管
route status=0/0 clean=1/1         是               是               attempts=16 calls=1 successes=1
6 类行（waiter-lock / park-about word / park-about target-word / park-return /
  ghost-disarm / judge-outcome）   28               32               == 各自 PI 段数，一段一条
window-trace-counters              0                0                按裁消失（环的元数据）
attempt-door                       28               32               PI 之外直打

逐字样例（run B）：waiter-lock a=0 b=0 ｜ park-about word value=0 ｜
  park-about target-word address 0x0000000000001896（递增）｜ park-return a=-1 b=110 ｜
  ghost-disarm a=-1 b=110 ｜ judge-outcome a=0 b=0 ｜ attempt-door a=1 b=0
```

## 与基线的差异及其原因（重要）

```
基线（冷机 pid 5168）6 类为 528/528/528/528/528/496，新 run 为"每段一条"（28/32）。
实测原因：撤环前 window_trace_reset() 全仓无调用者 => 环跨段累积 => 每次 flush 把累积记录重打一遍
  （证据：基线的 32 次 flush，counters 行 a=5,11,17,...,191 逐次递增 => 实际写入 160 条却印出 528/类，3.3 倍重复）；
  且 judge-outcome 因"写在本段 flush 之后"每段丢一条（496 = 528 - 32）。
结论：旧基线是"重打 + 丢失"的读数；新读数一名一条、无重复、不再丢。
判据已相应改为：每类 >= 1 且 == 该次 PI 段数；行名与取值形状逐字保留；counters == 0。
字段说明：park_wait_word / park_target_word 是必须的快照 —— 若不快照，park-about 会变成"flush 时刻读活字"，
  实测该列曾全 0（基线为递增）；加快照后恢复为递增的不同值（0x2925/0x2b30...、0x1896/0x1ad1...），与基线同形。
```

## 未验证项

```
1. 真机只做两次冷机，未做 KERNEL-PANIC-01 类反复复跑。
2. uid=0 仍未取得（真机 uid=0 计数为 0，w2 => w2-not-rooted，与基线同）。
3. run A 14 stage / run B 16 stage 属 run-to-run 变化，非本批引入（两次冷机本身即复核）。
4. T3（PI 内可调用面白名单机器化守卫）按裁后置，未接线。
5. 本批未碰 docs（口径更新由 docs 流另批完成）。

守卫对照组的**逐字原始输出**（base/A/B/C/C2）与字段快照的注释留痕，另见本目录：
`20261012-1820-pi-scope-annex-raw-outputs.md`。
```
