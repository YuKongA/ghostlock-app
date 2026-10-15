# 附件：搬迁面 · 删除次序 · 判据 b 口径（自 43499-port-six-targets-plan.md 拆出）

## 1. 搬迁面（约 2400 行）

```

race/     threads.{hpp,cpp} + pi_race.{h,cpp}                          约 271 行
route/    multicast_waiter_route.cpp + route_middleware.{hpp,cpp}
          + route_policy.hpp + route_controller.{h,cpp} + backend_contract.hpp   约 300 行
session/  全目录（14 件，含 exploit_session 与 runtime_config）          941 行
attack/   ops.{hpp,cpp} + session/backend/cve_2026_43499_backend.{hpp,cpp}      约 700 行
memory/   payload_builder.{cpp,h}                                       274 行
摘取      support/util.cpp 两件 + decls.hpp 声明 + kernelsnitch/utils.h 的 pin_to_core（改写）
          + route/tcp_zerocopy_route.cpp 的 disable_rseq_for_thread

```

依赖 24 个 token 的归类：在搬迁面内 16 个；需另补的独立件 8 个（ExploitSession · RouteController ·
encode_multicast_waiter · sched_setattr_tid · fail_stop_dirty_race · disable_rseq_for_thread ·
pin_to_core · runtime_config_snapshot 的定义）。

## 2. 删除次序（逐文件非测试引用数实测）

```

边缘件（引用 <= 2，可先删）
  steps/window_attempt.cpp(1) · steps/window_runner.cpp(1) · memory/write_encoder.cpp(1)
  · seam/multicast_route_linux.cpp(1) · access_factory_43499.cpp(1) · steps/access_binder.hpp(2)
枢纽件（引用 5-7，必须替换式，否则出现编不过的中间态）
  seam/multicast_route.hpp(7) · memory/multicast_arm.hpp(6) · memory/write_layout.hpp(6)
  · seam/multicast_window.hpp(5)
中间件（引用 3-4，同法）
  steps/window_attempt.hpp(3) · steps/window_runner.hpp(4) · memory/payload_page.hpp(3)
  · memory/credential_image.hpp(3) · access_multicast_waiter.hpp(3)
=> 对枢纽件：搬入的新件就位后，同批删旧件并把引用点一次改指。

```

## 3. 判据 b（cmp_disasm）的确切口径

```

用法：cmp_disasm.py [--reviewed] <baseline> <candidate>
  baseline  旧树二进制 —— make -C ../ghostlock-app-ref/src ghostlock
            （该 Makefile 的 mpack 计数为 0 => 独立于 P4 的 mpack 删除）
  candidate 新树二进制 —— 缺件，待新树恢复可构建

三档语义（工具 :198-221）
  strict    只把绝对地址归一到 0xH，保留符号注解与立即数   => 通过 = IDENTICAL
  operands  把 <...> 折叠为 <SYM>，保留立即数与结构偏移     => 通过而 strict 不通过 = 差异仅符号名/地址
  layout    <.*> => <SYM> 且所有 0x.. => 0xH               => 通过而前两档不通过 = 数值有差异但形状一致

通过条件（逐 target，6 条全过）
  底线 layout 相等      连它都不等 => 指令形状变了 => 算法变了 => 判失败
  合格 operands 相等    形状与数值都对，只差符号名/地址
  最强 strict 相等      IDENTICAL
  另加：无 MISSING（符号找不到）· 无 DIFF（指令条数不等）· 无 RELOC-DIFF（--reviewed 下 adrp 配对不等效）

符号层硬约束（由 MISSING 算失败推出）
  新二进制里必须存在这 6 个符号（或其候选拼写）
  => 搬入不得改函数名、不得进匿名命名空间；签名要命中 TARGETS 候选。

--reviewed 的等价规则（:224-232 注释逐字）
  "an equivalence rule over the dereference chain, never a symbol/address/index whitelist"
  adrp 配对：两有效地址相等，或两者都是从非写非执行段同宽读到字节相同的内容 => 归一；否则 RELOC-DIFF。

唯一已知例外
  pin_to_core 来自 kernelsnitch，而新树已无该目录 => 该 target 预计止于 layout 档，须在归档里单列。
## 4. 搬入的命名硬要求（2026-10-12 实测，漏了会以 MISSING 失败）

```

TARGETS 的候选拼写与新树现状：

  TARGETS 候选                                  新树现状（非测试）
  owner_thread / waiter_thread / consumer_thread 新树只有 ``*_main``（seam/multicast_route.hpp、run_pipeline_temp.cpp）
  run_main_route_threads                         新树无此名（0 命中）
  do_kernel5_fake_lock_route                     新树无此名（0 命中）
  do_one_write                                   新树 main.cpp 命中（可被 resolve）

=> 三个线程入口在新树里叫 ``*_main``，而候选取的是 `*_thread` => 若不改名，cmp_disasm 报 MISSING。
=> MISSING 算失败（见 §3），且失败形态是"MISSING"而不是"指令不一致"，定位更难。

所以替换式搬入必须同时改名：
  waiter_main / owner_main / consumer_main  =>  waiter_thread / owner_thread / consumer_thread
  run_main_route_threads 与 do_kernel5_fake_lock_route 必须以这两个名字存在
  do_one_write 保持一致（新树已有该名，可被 resolve）

## 5. 新树有两套窗口实现，都要改名并一并处理（2026-10-12 实测）

```
  seam/ 一套：seam/multicast_route.hpp 的 waiter_main / owner_main / consumer_main
  pipeline/ 一套：pipeline/run_pipeline_temp.cpp（1125 行）的 owner_main / consumer_main /
                 handshake_waiter_main（:921/:950/:973 由 pthread_create 启动）
  它自带原语：exit_child(:133) · effect_reached(:207, getuid()==0 判定) ·
              sched_setattr_nice(:234, SYS_sched_setattr) · FUTEX_WAIT_PRIVATE park(:203)
  它不引用旧树（grep 旧树路径与 旧码 均为 0 命中）=> 是自写的第二套
  它头部自称 TEMPORARY FILE, removal registered with the checklist，
  而两份审计在 docs/plan 下都查不到它的删除条目 => 该声明待核

  => 两套的入口名都是 `*_main`，而 TARGETS 要 `*_thread` => resolve 必然 MISSING。
  => 第 1 步删除清单必须列入 run_pipeline_temp.cpp（1125 行，最大单件之一）；
     第 2 步搬入应在其位置落地旧树实现；改名要在两处都做。
```

## 6. 第 1 步执行清单（删 / 留 / 待定，逐文件；只读量得）

```
删（我自己写的两套窗口实现，含其下层重叠件）
  seam/ 全目录                                                2232 行  refs<=7
  race/ 全目录                                                 455 行
  pipeline/run_pipeline_temp.{cpp,hpp}                         1158 行
  steps/window_attempt.{cpp,hpp} · steps/window_runner.{cpp,hpp} · steps/window_steps.{cpp,hpp}
  steps/access_binder.hpp · pipeline/access_factory.cpp
  memory/credential_image.* · multicast_arm.* · payload_page.* · write_encoder.* · write_layout.*
  => 小计约 4200 行（refs 除 seam 组外均 <= 6，无超级枢纽）

留（按 (甲)：被别处依赖的接口面与 ABI 链）
  route/ 全目录                                                510 行
  顶层 abi.hpp / abi.cpp · access_params.hpp · layout_from_document.hpp · access_*.hpp  1465 行
  pipeline/step_refusal.hpp  <- 本会话新建的步骤拒绝串权威表（20 verdict + main.cpp 19 处接线），不得误删
  steps/ 的非窗口件（action.hpp · route_config.* · targets.hpp · step_runner.hpp · prepare_* · words_region.*）

待定（与旧树功能重叠，需逐件对照后再定）
  memory/prepare_page.hpp(517) · prepare_plan.* · prepare_steps.hpp · heap_geometry.hpp ·
  heap_seam*.hpp · page_geometry.hpp · page_slot.hpp · reclaim_plan.* · shaping_plan.* · write_request.hpp
  leak/ 全目录（12 件，约 900 行）—— 旧树 support/ 有对应喷溅件，搬入后可能整组退场

连带改动（删时必须同批改的引用点，已实测）
  main.cpp:1211 · steps/window_attempt.hpp:23 · steps/window_runner.hpp:21 ·
  steps/access_binder.hpp:22/48/73 · access_factory_43499.cpp:15 · core/Makefile（含 NDK 行）

命名（见 §4/§5）：两套入口都是从 *_main 改回 *_thread，且 run_main_route_threads 与
do_kernel5_fake_lock_route 必须以此名存在。
```
