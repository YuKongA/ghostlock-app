# 附件：四类参数的字段表（自 jni-six-targets-params-plan.md 拆出）

## 6. 四类参数的字段表（第 2 步产出，实测）

```
A 类 配置：execution_settings 子集（profile/model.h:18-35，约 26 个 uint32，扁平可序列化）
  recommended_main_cpu · recommended_consumer_cpu · heap_prepare_max_attempts · heap_prepare_timeout_ms
  · heap_kernelsnitch_timeout_ms · race_route_wait_ms · race_route_done_timeout_ms · race_setup_settle_us
  · race_state_poll_interval_us · w1_attempts · w1_settle_us · w1_scratch_repair_attempts
  · w2_attempts · w2_settle_us · w3_chain_rounds · w3_attempts · w3_settle_us
  · tcp_attempts · tcp_arm_sequence · tcp_post_receive_hold_iterations
  · select_enter_delay_us · select_timeout_us · select_consumer_max_calls · select_consumer_burst_calls
  · handoff_* 五件

B 类 容器：PiRace 的数据成员（race/pi_race.h:65-88，27 个）
  uint32_t wait_futex · target_futex · chain_futex
  atomic<int32_t> waiter_ready · waiter_waiting · owner_started · owner_chain_done · owner_stop
                · route_done · waiter_tid · consumer_go · consumer_stop · consumer_calls
                · consumer_success · consumer_inflight
  atomic<uint32_t> route_delay_usec
  atomic<int32_t> fast_repair
  int32_t main_cpu · consumer_cpu
  PthreadOwner waiter_owner · owner_owner · consumer_owner
  const WriteRequest *request
  RouteStatus route_status
  方法（已是参数化形态）：reset(initial_delay_usec, main_cpu, consumer_cpu)
    · start_threads(waiter_entry, owner_entry, consumer_entry, route_request)  <- 三个函数指针参数
    · run() · request_stop() · join() · outcome_with_counters(outcome, calls, success)

入参：WriteRequest（memory/payload_builder.h:22-32）
  std::uintptr_t target · WriteMode mode · bool preserve_child
  enum class WriteMode : int32_t { Disabled = 0, Zero = 1, Credential = 2 }
   头注逐字：Immutable description of one kernel write. preserve_child selects the one-child
   erase layout; false selects the leaf/zero layout.
  make(target, mode, leaf) => preserve_child = !leaf

入参（续）：PayloadWriteLayout（同文件:34-40）
  std::uintptr_t parent · right · left · fops · bool needs_credential_copy

D 类 探测：selinux_off · enforce_readable · has_seccomp · direct_map{begin,end}
```

## 7. 总账与结论

```
字段总数约 65，形状全为标量/指针/原子，无嵌套、无动态容器（除 do_one_write 的标签字符串）。
=> "全部参数化"在数据面可行且不重：可打包成一个扁平结构跨 JNI，无需搬容器/迭代器/回调。

真正的工作量在语义搬运，三条边界：
  ① B 类的 14 个原子字段是三个线程的同步协议 => Kotlin 只能持有那块内存，不能替它们做同步。
  ② 三个线程入口必须是 C 函数（start_threads 本来就收函数指针，形态已支持）。
  ③ D 类四个探测可整体搬 Kotlin；其中 has_seccomp 决定要不要走 W3，是路径分叉点。

Kotlin 承接：配置 · 探测 · 编排 · 时机与重试判定 · 日志与结果判定
C 侧保留：六个核心函数 · futex/sched_setattr/pin_to_core/close 原语 · 三个线程入口本体
```

## 附录 B：旧边界 → 新边界（2026-10-13 核实后新增）

```
已确证的前提
  dev-nocpp 上 JNIEXPORT / external fun / Java_com_ghostlock / JNI_OnLoad 均 0 文件（唯一 loadLibrary 命中是归档文档）
  => 现在 Kotlin 与 native 之间不是 JNI 边界。

  旧边界是什么（两处证据）
    wire-schema.json 的 pending 第 ② 项："载荷字节的通道（data[] 只是位置；字节沿用 stdin/base64）"
      而用户裁定 ③ = 载荷＝stdin / base64
    run_pipeline_temp.{hpp,cpp} 头部自称 TEMPORARY FILE，登记于
      device-gates/20261009-1621-temp-entry-steps.md:3："临时入口驱动（pipeline/run_pipeline_temp.{hpp,cpp}），
      经 --ghostlock-app-call 启动"
  => 现边界是进程边界：Kotlin 拼 wire(bytes) -> exec native 进程 --ghostlock-app-call
     -> stdin 传 wire -> stdout 读日志/退出码；载荷字节走 base64。

新边界（本实验要做成的样子）
  Kotlin 调 nativeSessionCreate(configBlob, envBlob) -> jlong handle
  Kotlin 调 nativeRunOneWrite(handle, writePacked, label) -> jint（RouteStatus 压缩值）
  Kotlin 调 nativeSessionDestroy(handle)（按 AGENTS §5 顺序停访问 -> 解除关联 -> 回收）

退役项（换边界后不再需要）
  stdin/base64 承载  ·  --ghostlock-app-call 旗标  ·  run_pipeline_temp 的"临时入口"角色
  ·  那份"临时入口步骤"的登记条件（device + SA 材料）随之失效，需在清理归档里注明

保留项
  wire（若 Kotlin 仍要产 wire 给别处）· profile 生成链（Glkv3Encoder/Merger/Resolver）
  · HandoffPayloadSource 的设备侧载荷  ·  三个线程入口本体与 C 类原语
```

## 附录 C：三处必须参数化的硬编码（2026-10-13 读正文后新增）

```
① 目标锁的职责没有落点
   owner_main 头注逐字（run_pipeline_temp.cpp:272）:
     "The target lock is taken by PiRace::run(); taking it here too deadlocked the two."
   而新树没有 PiRace => 搬 owner_main 前必须先回答"谁取目标锁"。
   候选落点：JNI 上下文的 create 阶段，或新树 seam 层。

② consumer 的 nice 周期是硬编码
   run_pipeline_temp.cpp:297  const std::int32_t nice_value = (calls_this_seq % 19) + 1;
   19 这个周期写死在实现里；而 execution_settings（profile/model.h:18-35）里没有对应字段。
   => 按"全部参数化"应新增或映射一个配置字段，由 Kotlin 传。

③ 碰撞规则是硬编码的开发用桩
   run_pipeline_temp.cpp:312-328  struct StubBuckets
   注释逐字："TEMPORARY, registered with this entry: a rule that answers for one address only.
     Accepting everything made the walk answer the first candidate, which is address zero,
     and a zero object is rightly treated as no object. Accepting one chosen address makes
     the walk land on it, which is what this development run needs."
   实现：bucket() 返回 candidate == want ? 1 : 0
   => 碰撞规则本应来自 profile（它就是"某个地址落哪个桶"这条真规则的对立面），
      所以它属于"该由 Kotlin 提供"的那一类；实现文件里不该有桩。

=> 三条同属一类：实现里"偷偷替调用方做的决定"。本次实验正是要把它们翻出来交给调用方。
```

## 附录 D：第一批施工单（待落码）

```
1  新建 core/src/jni/，具名命名空间（例如 ghostlock::jni::race），目的：让符号出现在符号表里
2  搬四段（改名 + 移出具名空间，实现逐字不动）
     handshake_waiter_main (:166) -> waiter_thread
     owner_main            (:270) -> owner_thread
     consumer_main         (:283) -> consumer_thread
     increase_sleeper_main (:201) 保留原名（链内调用，不在 TARGETS）
   并搬五个状态结构：HandshakeWaiter · IncreaseSleeper · SchedAttr · ConsumerState · OwnerState
3  补两个新树缺失的名字（从旧树搬，属核心攻击函数）
     run_main_route_threads       旧树 race/threads.cpp:218
     do_kernel5_fake_lock_route   旧树 route/multicast_waiter_route.cpp:18
4  写三个 JNI 入口与上下文结构（第 7 节的映射）
5  处置三处硬编码（附录 C）
6  收尾：删 run_pipeline_temp.{hpp,cpp}（其独占内容已搬出）、删 main.cpp 的旧边界 48 行
    搬 Kotlin：effect_reached()（SELinux 与 getuid 判定）与附录 C 的三处决定
```
