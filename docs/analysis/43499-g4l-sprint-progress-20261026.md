# 43499 G4L 纯代码冲刺 · 进度台账

- 日期：2026-10-26（America/Toronto）
- 模式：用户知情强制「B 纯代码冲刺」（见 [43499-g4l-sprint-override-20261026.md](43499-g4l-sprint-override-20261026.md)；批准表 §4.2）
- 基线：`HEAD=9dd7e34c5849681857dc4274d4ab026ceef53df6`（2.0.10）
- 冲刺期唯一验证：`./gradlew :app:externalNativeBuildDebug --console=plain` 编译烟雾。门禁/评审/测试/真机全部延后补验批（task-9）。

## 进度

| 批次 | 状态 | 新增/修改 | 编译烟雾 | 语义选择 | 未验证/风险 |
|---|---|---|---|---|---|
| G4L-1 Placement | 已完成（代码） | `core/placement.c`(867L)、`placement_internal.h`、`core_internal.h`、`kernel_internal.h`；`CMakeLists.txt`；`api_unavailable.c` 删除 3 符号（余 6） | `./gradlew :app:externalNativeBuildDebug --console=plain` → BUILD SUCCESSFUL in 314ms, 4 tasks executed, exit 0（首轮 2 次失败已修：块注释内 `legacy*/`、`_GNU_SOURCE` 顺序） | ①scan_lower/upper=泄漏 mm_struct 接受窗口（须落 IDENTITY 区间）；scan_step=候选基址额外对齐（pow2 整除 allocation_size）；scan_round_limit=测量重试上限（≤8，每轮重建塑形）；②形状校验：objects_per_slab==25、prepare==8×25、spray==6×25、pre==24、post==25、page_size==4096、allocation_size==0x8000；③测量资源在 prepare 返回前释放，只有 children/memfd/staging 留给 landing；④cancel=同步空停止（stop_requested=1，不消费 token）；close 接受后 stop→close→reap，reap 不足 DIRTY/QUARANTINED 且对象保留不 free；⑤dirty 保留=不 free/不关，无全局登记表 | 真机门禁、失败注入矩阵、全门禁、nm/readelf、PI 反汇编、Kotlin DTO 实值对拍全部 UNVERIFIED；每轮 ≤400 child+401 fd 需真机测；scan step/round 语义为冲刺期自定，需真机校准 |
| G4L-2 Landing/Page | 已完成（代码） | `core/page.c`(17.2KB)、`page_internal.h`；`CMakeLists.txt`；`api_unavailable.c` 再删 land_page/close_page（余 4 Actor 符号；diff 纯删除 -112 行） | BUILD SUCCESSFUL in 309ms, exit 0（首轮 2 类失败已修：切除过度、`cpu_set_t` 需 `_GNU_SOURCE`→改裸 `SYS_sched_setaffinity` 掩码） | ①commit 点唯一=`sendmsg(shaping_pair[0], staging)` 返回 >0 之后；此前失败 NOT_APPLIED+placement_consumed=0，此后 commit_reached=1+placement_consumed=1；②提交后失败=DIRTY+QUARANTINED+reclaim_retained=1，Placement+Page 闭包整体保留不 free/不 close；③成功 receipt 计数真实（closed_pre/post/spray、reclaim_send_count），actual_page_base 强制==expected_page_base，content_generation 原样透传不参与判定；④成功收尾关闭 pre/post/spray 残余（新契约下 landing 即终结 Placement）；⑤closePage 接受即关 reclaim pair+free，owned_chain_released=1；⑥参数边界：socket_send_buffer_size∈[4096,4MiB]、reclaim_send_limit∈[1,16]、reclaim_payload_size==SKB_SEND_SIZE(0x10000) | 真机门禁、commit 前后失败注入、`/proc/self/fd` 归零、全门禁、nm/readelf、JNI receipt 对拍 UNVERIFIED；真机 pcp shaping 部分变化时按未提交回滚需真机注入确认；dirty 有意保留会增长 fd/RSS |
| G4L-3 Actor | 已完成（代码） | `core/actor.c`(1090L)、`carrier_internal.h`；`CMakeLists.txt`；`api_unavailable.c` 已删除（九入口全部真实实现：placement 3 + page 2 + actor 4，单一强定义） | BUILD SUCCESSFUL in 397ms, exit 0（首轮 2 类失败已修：`actor.h` 未含导致 setup 不完整；NDK30 bionic 无 `pthread_timedjoin_np`→退出标志+有界等待+pthread_join） | ①Page 提交点：Carrier+三线程创建并三线程就绪（prepare_timeout 内）后才 `page->state=2` 且 `page_consumed=1`；之前失败回滚 consumed=0；②partial-start/close 顺序：stop→drain→disarm→join(waiter→owner→consumer)→carrier release→page_release→free；任一步不干净=dirty retained；③PI 窗口唯一间接调用=prepare 绑定的 `waiter_trigger`（无 kind 读取/无 switch/无分配/日志/锁）；补验批若要求零 BLR，需改为每 kind 独立线程入口（跨 M/P/T）；④execute 恰好一次，二次=INVALID_ARGUMENT 不改物理状态；超时=dirty retained；⑤window 判定：waiter parked 超时=CLEAN_FAILURE/TIMEOUT，窗口开但 consumer_success=0=CLEAN_FAILURE/INTERNAL_ERROR；⑥cancel=停触发+排空+disarm，不 join/不消费/不销毁；⑦本批三 kind 仍返回 UNSUPPORTED（符合计划 §5「首个真实 Primitive 通过前」） | 真机门禁、三线程 cancel/partial-start 注入、PI 反汇编白名单、consumer nice 阶梯（本轮仅固定 nice）、全门禁、nm/readelf UNVERIFIED；busy-yield/200us 轮询 CPU 占用、select dup2 低号 fd（含 stdio）需真机确认；TCP 需第 4 punch 线程将作为 Carrier 内部资源（Actor.thread_count 仍 3） |
| G4L-4M Multicast | 已完成（代码） | `core/carrier_multicast.c`(171L)；`actor.c` 仅 4 个冻结分派点接 MULTICAST 分支；`CMakeLists.txt` | BUILD SUCCESSFUL in 397ms, exit 0（首轮 1 个指针类型告警已修→零 warning） | ①attempts=spec.operation_budget；arm 判据=setsockopt(MCAST_BLOCK_SOURCE) 返回 0 或 EADDRNOTAVAIL；arm_hold=编译期 0x1000（旧值来自 profile，需真机校准）；②address_family 仅 0/AF_INET；socket_buffer_size 仅上界校验不 setsockopt；③窗口内仅 syscall+内联原子+yield；④release 幂等，dirty 时 Actor 根本不调用 release | 真机单 Primitive 门禁、copy/walk 时序、窗口反汇编、失败注入、全门禁 UNVERIFIED；multicast 失败可能已污染内核栈（旧同形 fail-stop），真机确认 |
| G4L-4P Pselect | 已完成（代码） | `core/carrier_pselect.c`(337L)；`actor.c` 接 PSELECT 分支；`CMakeLists.txt` | BUILD SUCCESSFUL in 373ms, exit 0（首轮 2 错已修：缺 `<sys/timerfd.h>`、局部变量名与 pselect(3) 冲突） | ①frame=3 个 fd_set（in/out/ex，各 (nfds+63)/64×8B），frame_size 严格相等；②descriptor_capacity∈[4,1024]、operation_budget∈[1,1024]、timeout∈(0,1h]；③disarm 有界 spin，stuck=consumer_stuck=1→dirty；④stuck 时 release 不关任何 owned/pipe/timerfd，只恢复 stdio；⑤stdio 备份 F_DUPFD(nfds+64)，release dup2 恢复后关闭；⑥high_read 安装完即 close | 真机单 Primitive、stdio/进程 fd 恢复、consumer 卡死 dirty、窗口反汇编、失败注入、全门禁 UNVERIFIED；dup2 可覆盖任意低号 fd（含 0/1/2）为旧机制固有风险，真机必须验证不影响其它 fd |
| G4L-4T TCP zerocopy | 已完成（代码） | `core/carrier_tcp.c`(429L)；`carrier_internal.h`/`kernel_internal.h` 小增；`actor.c` 接 TCP 分支；`CMakeLists.txt` | BUILD SUCCESSFUL in 412ms, exit 0、0 warning 0 error | ①frame=zc 请求体模板，Native 只写 address=mapping_base+mapping_offset（offset 0），每 attempt 重拷模板；②punch worker=Carrier 内部第 4 线程（prepare 建、release 有界等 punch_exited→join→munmap→close），`Actor.thread_count` 仍 3；③release/join/munmap 失败→Actor 整链 retained；④mapping_offset 页对齐且 +page_size<=mapping_size；mapping_size∈[2页,64MiB]；receive_size/frame_size∈(0,4096] 且 frame>=16；⑤punch/arm 时序用编译期常量（旧值来自 profile，需真机校准） | 真机 TCP 单 Primitive、zerocopy 命中/映射形态、punch 时序、窗口反汇编、失败注入、全门禁 UNVERIFIED；`struct tcp_zerocopy_receive` 首成员偏移 0 依赖内核 ABI（补验需反汇编/真机确认） |

| G4L-5 Kotlin/Lua wrapper | 已完成（代码） | `backend/cve43499/owned/{TokenOwner(348),PlacementOwner(358),PageOwner(207),ActorOwner(141)}.kt`（NEW 4，共 1054L）；零改既有文件 | `./gradlew :app:compileDebugKotlin --console=plain` BUILD SUCCESSFUL in 691ms, exit 0（首轮 1 类型错误已修） | private token+单对象锁；`callStatus==Int.MIN_VALUE` 才恢复 token；consumed 不回滚；clean 保留/dirty 消费；close 后/双 close/重复 execute 本地拒绝；无 Cleaner；Placement→Page→Actor 转移与 receipt 对拍；物理常量具名（PAGE_SIZE/ALLOCATION/OBJECTS_PER_SLAB 等） | 宿主矩阵（正常转移/clean 保留/dirty 消费/close 后拒绝/双 close/异常 finally/R8/九入口对拍/Lua 反射契约）UNVERIFIED；Kotlin 与 Native 常量重复需补验对拍；execute 物理成功≠uid |
| G4L-6 victim/Finish/ksud | 已完成（代码） | `victim/{VictimProbe(35),ProcessVictim(112),ProcessVictimSession(146)}.kt`、`finish/{FinishHandoff(92),FinishAcceptance(190)}.kt`、`run/Cve43499Run.kt(290)`、`ui/.../FinishHandoffAdapter.kt(55)`（NEW 7，共 920L）；零改既有文件 | `./gradlew :app:compileDebugKotlin :app:externalNativeBuildDebug --console=plain` BUILD SUCCESSFUL in 699ms, exit 0 | victim=Kotlin ProcessBuilder、有界幂等 close、uid 超时清理；handoff 由 UI 生成（后端 0 import ui）；验收 6 项证据（uid==0/脚本 size+SHA-256/ksud·apd/marker/SELinux=1/资源终结）缺一即具名拒绝；run 显式构造、不接默认 Flow（Flow43499Impl 仍 not-wired） | 真机 uid/SHA/ksud/marker/SELinux、冷机门禁、宿主矩阵、R8、assembleRelease UNVERIFIED；ksud 仅文件存在性证据（不证明 late-load）；G4L-6 生产入口尚未接线 |

## 最终状态（Lead 独立复核，2026-10-26；G4L-5/6 更新）

- **六批代码全部完成**：G4L-1..G4L-4T 实现位于 `app/src/main/cpp/gl43499/core/`，共 11 个新 C11 文件 + 4212 行；`CMakeLists.txt` 的 `ghostlock43499` = jni43499.c + placement.c + page.c + actor.c + carrier_multicast.c + carrier_pselect.c + carrier_tcp.c；`api_unavailable.c` 已删除（九入口全部真实实现，每符号单一强定义）。
- **Lead 独立编译**：`./gradlew :app:externalNativeBuildDebug --console=plain` → BUILD SUCCESSFUL in 308ms，4 tasks executed，EXIT=0。
- **Lead 独立符号核验**：`libghostlock43499.so`（234728 B，sha256 `550baeba19832f1b08842525eb6cddc3edbcc1ba8370335cb80103f873143c57`）：9 个 JNI 导出；9 个 core 入口各恰好 1 个定义（hidden `t`）；`.data/.bss` 可写全局 0；旧 Flow/session/route/registry 符号 0。
- **零改动**：公共 C11 头 7 个、`jni43499.c`、Kotlin/profile、`legacy*/`、`glgeneral`、43284、UI。
- **Kotlin 侧（G4L-5/6）**：11 个新文件、1974 行，零改既有文件；Lead 独立编译 `./gradlew :app:compileDebugKotlin :app:externalNativeBuildDebug --console=plain` → BUILD SUCCESSFUL in 699ms，EXIT=0；`backend/**` 零 `import ui`；token 全 private、无 Cleaner；`Flow43499Impl` 仍 not-wired（生产入口不存在）。
- **未验证**：真机冷机门禁（无设备）、失败注入、fd/线程/child 归零、PI 反汇编、Gradle 全门禁、G4L-0 FAIL 三项、5 个先存红测试、Kotlin DTO 实值对拍、G4L-5 wrapper 矩阵、G4L-6 真机验收（uid=0/脚本 SHA/ksud/marker/SELinux/资源终结）、R8/assembleRelease——全部转 task-9 补验批。
- 未 commit/push；工作树新增内容待用户决定提交或先补验。

## 范围/卫生观察

- 公共 C11 头（abi/result/placement/page/actor/primitive/api.h）保持零改动；`jni43499.c`、Kotlin、`legacy*/`、`glgeneral/` 零改动（冲刺期核对）。
- 编译烟雾会改写被跟踪的 `app/.cxx/**` 构建产物（2.0.10 已提交过同类产物）；不属交付源码，补验批统一清理/排除。
- 索引仍有基线期 15 个 `AD` 幽灵条目，未触碰。

## 补验批待办（task-9）

G4L-0 FAIL 三项（B1/B2/B3）、5 个先存红测试与 plan §6 命令修正、逐批 design-critic/native-core、宿主失败注入、Gradle 全门禁、四事务产物+回滚、nm/readelf/APK、UML/评审记录回填、真机冷机门禁（4M/P/T 各自单 Primitive）。

## 用户终验后结构修复（2026-10-26，F-A～F-D）

| 修复批 | 阻断 | 结果 | 证据 |
|---|---|---|---|
| F-A（task-12） | 1 PI 动态分派 / 2 Page 所有权 / 6 Carrier 成功判据 | 完成 | 函数指针 grep=0；dirty 前真实收养 Page；`carrier_outcome` + `observed_write&&calls>0&&successes>0` |
| F-B（task-13）+ F-B+（task-17） | 3 PSELECT fd / 5 清理记账 / 7 TCP 溢出 | 完成 | 目标 fd 全集备份/恢复、double-close=0；清理 clean 出参+`page_release_checked` 接入 closeActor；`__builtin_add_overflow` 审计 |
| F-C（task-14） | 4 JNI 原子回执 | 完成 | `callStatus` 最后写；NOT_ACCEPTED/INCOMPLETE 哨兵；Kotlin 三态 fail-stop |
| F-D（task-15） | 8 Finish 执行链/marker/ksud / 9 victim-Actor 绑定 | 完成 | `FinishExecution` + `GHOSTLOCK_RUN_ID` marker 绑定 + ksud 命令证据；`RunBindingEvidence`（pid/starttime/uid+三地址） |
| F-E（task-16，Lead） | 10 文档一致性 | 完成 | 结构修复记录 + 完成记录快照声明 + 批准表状态更新 |

- Lead 独立编译：`./gradlew :app:externalNativeBuildDebug :app:compileDebugKotlin --console=plain` → BUILD SUCCESSFUL in 725ms，EXIT=0。
- 当前状态：**实验代码、结构修复完成、待独立复核与真机门禁**；仍不得标 PASS/supported/生产可用；task-9 补验批未启动。
- 修复细节/残余限制见 [43499-g4l-structural-fix-20261026.md](43499-g4l-structural-fix-20261026.md)。
