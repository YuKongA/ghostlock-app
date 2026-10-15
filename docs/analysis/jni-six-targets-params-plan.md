# 实验：6 个 cmp_disasm 目标参数化 + JNI 化（分支 dev-nocpp · 2026-10-13）

> **状态（2026-10-14）**：本件 §4/§7 的**边界形状已被取代** —— 见 `docs/analysis/jni-two-backend-plan.md`（四 extern + GLKJ 帧 + op 表 + Kotlin 高阶步骤）。
> §1–§3 的参数化调查与附件 `jni-six-targets-params-annex-fields.md` **保留为参考**，不得再作为边界权威。

## 0. 目标（用户逐字）

> 「先分析这6个cmp_disasm的所有入参和读取外部的信息，列出他们，把他们全部参数化，
> 然后将起封装为jni库，除了核心攻击函数以外，剩下的操作由kotlin完成」

分支：dev-nocpp（名即意图）。L 级：先出本计划，获认可后写码。

## 1. 6 个目标的签名（旧树实测）

```
race/threads.cpp:18   void *waiter_thread(void *arg)
race/threads.cpp:75   void *owner_thread(void *arg)
race/threads.cpp:98   void *consumer_thread(void *arg)
race/threads.cpp:218  Status run_main_route_threads(const memory::WriteRequest &request)
route/multicast_waiter_route.cpp:18  route::RouteStatus do_kernel5_fake_lock_route(const memory::WriteRequest *)
attack/ops.cpp + session/backend/cve_2026_43499_backend.{hpp,cpp}   do_one_write(WriteRequest*, char const*)
   （ops.cpp 里无字面名，走别名；见 cmp_disasm TARGETS 第 6 条的候选列表）
```

## 2. 入参表（形参层）

```
目标                          形参                              实参语义
waiter_thread                 void *arg                         PiRace*（首行 static_cast<PiRace*>）
owner_thread                  void *arg                         同上
consumer_thread               void *arg                         同上
run_main_route_threads        const WriteRequest &              本次写的目标（地址/值/宽度）
do_kernel5_fake_lock_route    const WriteRequest *              同上（指针形态）
do_one_write                  WriteRequest*, char const*        本次写 + 标签（用于日志）
=> 形参层极薄：三个线程入口只收一个上下文指针，另三个收 write 请求与标签。
   真正的输入面在于它们经由上下文读到的东西 —— 见第 3 节。
```

## 3. 外部读取表（要参数化的清单）

```
A 类 profile 数据（6 个取值点，纯数据，可直接搬 Kotlin）
  profile.race_state_poll_interval_us()        waiter_thread:28 · owner_thread:83
  profile.race_route_wait_ms()                 waiter_thread:39
  profile.select_consumer_burst_calls()        consumer_thread:122
  profile.has_compact_waiter()                 consumer_thread:131（决定 consumer_nice）
  profile.select_consumer_max_calls()          consumer_thread:149
  profile.multicast_layout()                   do_kernel5:22（结构，非标量）
  另两处整传：controller.init(race, &profile)  waiter_thread:51
              route_needs_ghost_disarm(profile) waiter_thread:56

B 类 会话状态（线程间共享，不能变成值参数，只能变成"由调用方提供的容器"）
  race.start_threads / route_status / route.run() / request_stop / join   run_main_route_threads:221-244
  race.consumer_calls / consumer_success / consumer_stop / route_delay_usec /
  consumer_go / consumer_inflight                                        do_kernel5:67-95
  heap.current.fake_task / fake_lock                                     do_kernel5:44

C 类 syscall 与平台原语（必须留 C 侧：跨 JNI 的每次 futex 都会破坏 PI 窗口时序）
  support::futex_op                全部 5 个函数里都出现（waiter 3 · owner 3 · consumer 3 …）
  support::disable_rseq_for_thread waiter:21 · owner:77 · consumer:100
  support::sched_setattr_tid       consumer:134
  support::fail_stop_dirty_race    run_main_route_threads:226/:239/:244
  kernel::pin_to_core              consumer:101
  kernel::PSELECT_CONSUMER_NICE    consumer:133
  syscall(SYS_gettid) · SYSCHK(clock_gettime) · usleep · sched_getcpu · close

D 类 环境探测（只有第 6 个目标读它们，且其中一件决定攻击路径 —— 最该由 Kotlin 提供）
  check_selinux_off()      读 /sys/fs/selinux/enforce，返回是否 '0'
  enforce_readable()       同上，返回是否可读
  process_has_seccomp()    读 /proc/self/status 的 Seccomp: 字段
     头注逐字："The app flow runs inside zygote, whose seccomp filter blocks finit_module(2).
     The adb/shell flow has no filter (Seccomp: 0), and fork() inherits that,
     so the W2 child does not need W3 there."   => 它决定要不要走 W3，是路径分叉点。
  in_direct_map(t)         读 kernel::DIRECT_MAP_BASE 与 kernel::g_direct_map_end
  exploit_t0()             static struct timespec（全局可变时间基准）
  另六件外部动作（实现在 exploit_ops.cpp）：install_profile · resolve_profile_addresses ·
     apply_iomem_cache · slab_drain · write_root_script · perf_find_task
```

## 4. 由分析推出的 JNI 边界形状

见第 7 节（第 3 步的设计草案）：C 侧保核心与原语；Kotlin 提供 A/B/D 三类；
每个核心函数收「上下文句柄 + 少量真参数」。

## 5. 后续步骤

```
第 2 步（待认可）：定 A 类结构体与 D 类参数的字段表，并标出每个字段由 Kotlin 哪一处产生。
第 3 步：定 B 类上下文句柄的 JNI 表示（谁分配、谁释放、生命周期与线程可见性）。
第 4 步：实现 —— 6 个函数逐个改签名收参数；Kotlin 侧承接编排与探测。
第 5 步：判据 —— cmp_disasm 三档（layout 为底线）必须仍能跑；真机四读数不变。
```

## 6. 四类参数的字段表（全文见附件）

A 配置（execution_settings 子集 ~26 个 uint32）· B 容器（PiRace 的 27 个数据成员）·
入参（WriteRequest 3 字段 + PayloadWriteLayout 5 字段）· D 探测（4 项）—— 逐字段清单见
jni-six-targets-params-annex-fields.md（约 3 KB）。

字段总数约 65，全为标量/指针/原子，无嵌套、无动态容器（除 do_one_write 的标签字符串）。
=> 数据面可打包成一个扁平结构跨 JNI；真正的工作量在语义搬运（见附件末节的三条边界）。

## 7. 第 3 步：JNI 边界设计（草案，待认可）

```
既有范式（本树对跨边界类型的纪律，两处先例）
  BorrowedFd    static_assert 2 条：可平凡拷贝 · sizeof == 4            （native_resource.hpp:30-31）
  RouteStatus   static_assert 7 条：标准布局 · 可平凡拷贝 · 固定 20 字节 ·
                对齐到 int32_t · 5 个 offsetof 逐一钉死                  （route_status.h:37-45）
  => 规则：凡可能跨语言边界传递的类型，布局必须钉到字节级；
     alignof == alignof(int32_t) 即"可当作一串 int32 传"的正式许可。
  => 故 JNI 映射不需重新设计，照此补 assert 即可。

三个 JNI 函数（对应 initialize -> shutdown 语义）
  jlong  nativeSessionCreate(byte[] configBlob, byte[] envBlob)
           C 侧分配 PiRace 等价物；写入 A 类配置与 D 类探测结果；返回不透明句柄
  void   nativeSessionDestroy(jlong handle)
           按 AGENTS §5 顺序：停止访问 -> 同步/解除关联 -> 回收
  jint   nativeRunOneWrite(jlong handle, jlong writePacked, jstring label)
           六个核心函数的统一入口；返回 RouteStatus 压缩值（见下）

类型映射
  RouteStatus        -> 5 个 jint（它已具备"按 5 个 int32 传"的保证）
  WriteRequest       -> 打包进一个 jlong（target + mode + preserve_child）
                        需在 JNI 侧补一组 static_assert 说明 bit 位分配
  PayloadWriteLayout -> 5 字段，只在 C 侧用，不跨界
  PiRace             -> 不透明句柄 jlong；C 侧分配与持有
                        理由：含 std::atomic 与 pthread_t，且三线程按偏移访问同一块内存

线程约定
  三个线程入口本体留 C（start_threads 本来就收函数指针，形态已支持）；
  启动时机由 Kotlin 决定（一次 nativeRunOneWrite 触发一次尝试）；
  B 类的 14 个原子字段只被 C 侧读写；Kotlin 需要的结论经返回值带出。
```
