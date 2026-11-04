# 43499 W1→W3 × 三 route 调用序列矩阵（2026-10-28，只读分析）

- 状态：**只读交付（task-61），零生产代码改动、不 commit**。合并既有四路审计，补齐「Flow step→JNI→Native core→每 syscall/libc（file:line+参数/顺序/返回）→ legacy 对照」矩阵。
- 快照：2026-10-28T12:4x EDT，HEAD `a9af5482`+工作树（task-52..60 未 commit）。**写者 r3e-impl 已停手**；`jni43499.c` sha256=`d5353842…`（N01 已确认）。风险见 §7。
- L1=legacy（C++ refactor）、L2=legacy2（2.0.8 core）、VL=very_legacy。严重度 S1/S2/S3；证据 `DEV(E0/E4报告)`/`source-only`/`UNVERIFIED`。
- 附件（完整有序调用表，不设字节或篇幅门）：[M multicast](43499-w1-w3-three-route-call-matrix-20261028-annex-multicast.md)、[P pselect](43499-w1-w3-three-route-call-matrix-20261028-annex-pselect.md)、[T tcp](43499-w1-w3-three-route-call-matrix-20261028-annex-tcp.md)。
- 已有审计（不重复）：[四路逐 syscall](43499-syscall-reaudit-20261027.md)、[三路 review](43499-three-route-line-review-20261027.md)、[perf-G1](device-gates/perf-g1-20261027.md)、[perf 设计](plan/43499-perf-leak-primitive-plan.md)。增量=J helper 链、W1b/fast-repair、J 自采样/raw relay、victim/finish、cleanup。

## 0. 结论摘要

1. **规模**：主件 20 阶段行（S01–S20）+ 附件 M17/P18/T18 = **73 行**；三 route 的 W1/W1b/W2/W2b/W3 写事务共用 Kotlin/JNI/Actor 链，差异仅在 carrier prepare/arm/trigger/disarm/release（附件逐 syscall）。
2. **J 主路径闭合（source-only）**：`JniVictimTargetSource.acquire`→`VictimSession.perfSelfSample`→JNI 帧→`posix_spawn` helper→`perf_event_open(pid=0)+mmap+ioctl+getpid×N+memcpy→raw memfd`→JNI raw→Kotlin 解析/投票。E0/E4 A301SO 报告 PASS；E1/E2/E5 未跑。
3. **新发现 10 项**（N01–N10，只报不修；1×S1 条件、7×S2、2×S3）；**未实现 parity 18 项**（§5）。
4. **最大门禁风险**：W3 三 route 不可成功（leaf verifier UNSUPPORTED）；pselect per-attempt 超时缺失；dirty 无 fail-stop；J helper perf attr 字段未透传。

## 1. 调用层级（权威映射）

```text
Flow43499Impl.kt:373-383 (writeChainSteps) / :216-259 (singleWriteSteps)
 └ Cve43499WriteTransactionRunner.kt:49 execute
   └ Cve43499Lifecycle.kt:260/295/329/363/402/418（唯一生命周期层）
     └ owned/{Placement,Page,Actor}Owner → Native43499Jni.kt:353-391
       └ jni43499.c（HEAD:a9af5482:532/581/588/595/637/657/692/734/741；快照缺 N01）
         └ core/{placement,page,actor,carrier_*}.c（POSIX 机制）
J 路径：Flow:929/633 → victim/Cve43499JniVictim.kt:88-306 → core/victim/VictimSession.kt:127/163/181/219/258/281/304/342/373
 → VictimTransportJni(jni:998-1305) → core/victim_protocol.c:356/465/476/507/532/594/603/635/643
 → helper(core/victim_helper/victim_helper.c:840→:791→:296/309/462/720/733)
```

## 2. 阶段×调用矩阵（共享链；route 分支=附件）

| # | 阶段 | Kotlin→JNI→Native | syscall/libc 域（file:line；详表见附件） | route | legacy（L2/L1/VL） | 差异/证据 |
|---|---|---|---|---|---|---|
| S01 | request.validate | Flow:392→Runner 构建 :392-424 | 无 | 无 | L1 backend:41-93 | 同构/S3/source |
| S02 | W1 placement.prepare | Flow:425→Runner:49→Lifecycle:260→PlacementOwner.prepare→`preparePlacement`(kt:353/jni HEAD:532)→placement.c:783→acquire:361 | clone:92/110；prctl:94；open(/proc/pid/mem):133；waitpid:146/532；usleep:552；kill/close:215-243 | 共用 | L2 support.cpp:555-725；VL util.c:865 | 参数化等价/S3/source |
| S03 | W1 page.land | Runner:78→Lifecycle:295→PageOwner.landPage→`landPage`(kt:365/jni HEAD:595)→page.c:214 | socketpair:301/326；setsockopt:310；fcntl:317-319；sendmsg shaping:358；yield:373/389；close memfds:391；sendmsg reclaim:401；close:485/492 | 共用 | L2 support.cpp:727+memory.cpp:210-227；VL util.c:814 | 等价/S3/source |
| S04 | W1 actor.prepare | Runner:104→Lifecycle:329→PageOwner.prepareActor→`prepareActor`(kt:377/jni HEAD:657)→actor.c:781→carrier_prepare:507 | pthread_create×3；futex:120；sched_setattr:154；carrier prepare（附件） | carrier prepare | L2 race.cpp:53-115；L1 threads.cpp:18-135 | nice 轮转已修/S3/source |
| S05 | W1 actor.execute | Runner:129→Lifecycle:363→ActorOwner.execute→`executeActor`(kt:385/jni HEAD:692)→actor.c:1008 | futex CMP_REQUEUE_PI/WAIT/WAKE；carrier trigger（附件）；join | carrier trigger | L2 route_*.cpp execute；L1 backend:469-545；VL main.c:491/559 | 等价/S3/source |
| S06 | W1 verify+closeChain | w1Done:549；verifyW1 WriteChain.kt:376；closeChain Runner:178→Lifecycle:402 | closeActor/Page/Placement(kt:359-391)→actor.c:1201/page.c:453/placement.c:944 | route release | L2 race.cpp:113；L1 收尾 | W1 verify 可 UNSUPPORTED 继续/S3/source |
| S07 | W1b repair | Flow:572→Runner:49（W1B request；quarantine=保留 W1 链；w1bAttempts/w1bSettleUs 重试/退避） | 同 S02–S06（目标/模式 generator） | 共用 | VL main.c:1320-1370；L1 backend:398 | 等价/S3/source |
| S08 | W2 baseline/pump/precheck | Flow:633；baseline:1248；pump:1266；precheck:1293 | 无（J 迭代在 helper） | 无 | VL main.c:939-964；L1 victim_process.cpp:36-41 | J 路径差异/S3/source |
| S09 | W2 perf 选址（J） | JniVictimTargetSource.acquire:352→samplePass:426；parser/vote | J：adapter:294→VictimSession:181→send/receive(jni:1078/1110)→protocol:465/476；helper:309→perf_event_open:343/mmap:351/ioctl:362-374/getpid:371/memcpy:400→raw memfd；readRaw(jni:1152)→protocol:507；Kotlin SHA/2-of-3；parser 接受 ABI=1/2/280B（:190-216） | 无（self） | L2 attack.cpp.disabled:444-522；L1 ops.cpp:434-513；VL main.c:844-925 | 自采样=legacy；attr 透传缺口 N04/S2/source；DEV(E0/E4 机制) |
| S10 | W2 credential 事务 | Flow:633→Runner:49（Credential） | 同 S02–S06（mode=Cred、目标=victim cred） | 共用 | VL main.c:1102-1160；L1 backend:220 | 等价/S3/source |
| S11 | W2b fast-repair | Flow:700-756→Runner:49（W2_REPAIR）→fastRepairActorSetup:167 | 同 S02–S06（init_cred+8） | 共用 | VL main.c:1142-1148；L1 fast-repair | 新架构重跑完整 placement（无预建）/S2/Kotlin-B2 |
| S12 | W2 uid/rooted | Flow:970→lifecycle.victimUid:513→adapter:200→session.probeUid:163 | protocol:296 getuid；JNI send/receive | 无 | VL main.c:967；L1 victim_process 'C' | 等价/S3/source |
| S13 | W3-0 leaf 探针 | Flow:757/873→Runner:49（W3 comm probe；leaf 方向 EXACT/TARGET_PLUS_8，探针不可用=第三态 fail-closed） | 同 S02–S06（leaf 方向） | 共用 | L2 route_select.cpp:305-357；L1 backend:270；VL main.c:1370 | **W3 不可成功（leafDirection=null）**/S1/Kotlin-B3 |
| S14 | W3 flags/mode | Flow:757→Runner:49×2 | 同 S02–S06（TIF_SECCOMP/thread_info.flags、seccomp.mode） | 共用 | L2 memory.cpp:265-282；L1 backend:300-364；VL main.c:1424-1434 | 等价；verify UNSUPPORTED/S2/source |
| S15 | W3 verify/链轮 | Flow:757；verifyW3 WriteChain.kt:399；restarter 默认 null→1 轮 | 无 | 无 | VL main.c:1456；L1 backend:528 | UNSUPPORTED⇒fail/S2/source |
| S16 | victim start/spawn | Flow:929→Lifecycle:242→默认 ProcessVictimSession:62/72 或 J adapter:185→session.start:127 | J：socketpair:373；memfd_create:378；ftruncate:389；posix_spawnattr/file_actions:392-425；posix_spawn:427；helper close_range:213/assert:235 | 无 | L2 无 helper；L1 victim_process:192；VL main.c:1090 | J=posix_spawn（用户约束）/S2/source；DEV(E4) |
| S17 | victim identity | Flow:1248→Lifecycle:499→adapter:214→session.identity:373 | Kotlin 读 `/proc/<pid>/stat`（core/victim/VictimIdentity.kt:36-49） | 无 | VL main.c:939；L1 victim_process:36-41 | 等价/S3/source |
| S18 | finish 'G' | Flow:1005/1018→Lifecycle:533→adapter:229→session.execScript:219；fallback:1069 拒绝 | JNI send/receive；helper:462 posix_spawn(script)+poll/read+waitpid+timeout kill | 无 | VL main.c:1044-1056；L1 victim_process:136-154；L2 `.disabled` | exec 仅脚本/S3/source |
| S19 | close/retained | Flow:1107→Lifecycle.closeAll:418；J closeVictim(jni:1232)/detach(jni:1223) | kill/waitpid/reap、close/munmap、futex stop；dirty=retained | route release | VL main.c:930；L1 backend:453 | dirty 无 fail-stop/S1/Native-B1 |
| S20 | acceptance | Flow:1131；FinishAcceptance Kotlin | 无 | 无 | legacy handoff probe | 等价/S3/source |

> 完整 syscall 参数/顺序/返回与三 route 分支见附件（M/P/T 编号与 S 交叉引用）。

## 3. 跨 route 差异

| 项 | multicast | pselect | tcp |
|---|---|---|---|
| prepare | socket(AF_INET,SOCK_DGRAM\|SOCK_CLOEXEC) mc:95；frame 深拷贝；arm 参数校验 mc:60-90 | pipe ps:390；timerfd_create ps:401；备份 F_DUPFD_CLOEXEC ps:216；dup2 install ps:260 | socket/bind/listen/getsockname/connect/accept4 tcp:82-133；memfd_create:387；fallocate/ftruncate；mmap:399；punch pthread:410 |
| trigger/arm | setsockopt(MCAST_BLOCK_SOURCE) mc:142（attempts 循环）；arm≥16；arm_hold spin mc:158 | pselect/select ps:507/513；enter_delay ps:469；set reload ps:487 | send(MSG_DONTWAIT) tcp:465；punch phase spin tcp:474/481；zc 四字段 tcp:503-517；getsockopt:518 |
| disarm | go=0+inflight spin；ghost FUTEX_LOCK_PI actor.c:440 | stuck 不恢复/不 dup2 ps:550-556 | punch_stop；等 inflight |
| release | close(socket) mc:203 | restore backups/close sources ps:118-195 | munmap→close tcp:590-612 |
| 主要差异 | B2/B7 已修/S3 | per-attempt 超时缺失（S1）、fd install 窗口（S1） | T1–T6 已修/S2 |

## 4. 四路审计覆盖与本矩阵增量

已覆盖：Kotlin-B1..B6、Native-B1..B9、perf-B1..B9、JNI-L1..L4（syscall-reaudit §1）；三 route B/P/T（review §1–§4）；G1/perf 证据。增量：J helper 全链、W1b/fast-repair、W2b/J 选址/ABI=2、finish execScript、cleanup/detach、carrier 逐 syscall。仍缺：E1/E2/E5 原始日志、pstore、`/proc` fd 实测、route 命中率。

## 5. 尚未实现的 parity（file:line；只报不修）

| # | 项 | 现状 | legacy 目标 | 严重度 |
|---|---|---|---|---|
| G01 | dirty fail-stop | 只 retained 继续（actor.c:1252/placement.c:308/page.c:481） | L1 backend exit_group(70)（review Native-B1） | S1 |
| G02 | pselect 每 attempt 超时 | 用剩余总 deadline（ps:497-513） | L2 route_select.cpp:457-470 | S1 |
| G03 | pselect 每 attempt 重喷页 | 单次 landing，只重建 fd_set | L2 route_select.cpp:424-432 | S2 |
| G04 | W3 不可成功 | leafDirection=null（WriteChain.kt:397）⇒ W3 恒 fail-closed | L2 memory.cpp:265-282 | S1 |
| G05 | fast-repair 预建 | W2 后重跑完整 placement（Flow:700-756） | VL main.c:1142-1148 | S2 |
| G06 | RLIMIT | 无 getrlimit/setrlimit（placement.c） | L2 support.cpp:373-415 | S2 |
| G07 | KSU 守卫 | 无 `ksu_root_owned`（Flow:154-230） | L1 backend:398-410 | S2 |
| G08 | CPU pin | waiter/owner 新增 pin（actor.c:231/306） | L1 threads.cpp:135-138 | S2 |
| G09 | SIGPIPE | app 写路径无 SIG_IGN（victim_protocol.c:171-193） | L1 backend:431；VL main.c:1264 | S2（N05） |
| G10 | ABI=2 接受/验证 | parser 已接受 ABI=1/2 并按 280B/8B regs 解析（Cve43499PerfSampleParser.kt:190-216）；E2 未跑，helper 透传 attr 后 ABI 分布未知 | L1 ops.cpp:484（ABI 1/2 均收） | S2（证据缺口） |
| G11 | helper perf attr | exclude_user/hv/idle/disabled/auto_enable 未透传（N04） | L1 ops.cpp:434-513 | S2（N04） |
| G12 | arm_hold 单位 | 字段 `arm_hold_ns`=spin（mc:158） | L2 route_multicast.cpp:101-131 | S3 |
| G13 | ghost disarm | waiter FUTEX_LOCK_PI actor.c:440+回执 | L1 backend D3 | S2 |
| G14 | post_hold 单位 | `post_hold_ns`=spin（tcp:530） | L2 route_tcp.cpp:344-352 | S3 |
| G15 | backup fd 起点 | nfds+fd_reserve_stride（ps:216） | L2 route_select.cpp:238-266 | S3 |
| G16 | 16MiB mapping | Kotlin generator 参数（max_mapping） | L2 route_tcp.cpp TCP_PUNCH_SHMEM_LEN | S3 |
| G17 | victim 行协议 | J 帧协议；task-45 行协议 fallback | task-45 | S3 |
| G18 | close/park 复检 | J detach/close 与 legacy park 语义一致、无复检 | VL main.c:930 | S3 |
| G19 | pselect install 窗口 | fd 备份/`dup2` 安装发生在 **prepare**（carrier_pselect.c:409-442），跨 JNI 返回后才 execute ⇒ ART 可能触碰被覆盖的 JVM fd（Native-B5） | L2 route_select.cpp:351-407（prepare 只建 fdsets）；安装应在 execute 首次 select 前同调用完成 | S1 |

## 6. 本轮新发现（只报不修）

| # | 发现 | file:line | 最小修复 | 严重度 |
|---|---|---|---|---|
| N01 | 快照 `jni43499.c` 缺 7 个 Placement/Page/Actor JNI 导出（HEAD:532/581/588/595/637/657/692/734/741 有；当前仅 cancelActor/closeActor/perf/victim 共 17 个）。writer 已停手且该文件 hash=`d5353842…` 不变 ⇒ **W1/W2/W3 的 Flow 路径首次 JNI 调用 `UnsatisfiedLinkError`**（E1 harness 同样受影响）；NDK/单测不检查符号故门禁未发现 | `git diff -- jni43499.c`（-213 行）；当前 `grep Java_com_`=17 | 恢复 7 个导出；加 JNI 符号 dlopen/dlsym smoke 或 instrumented 自检 | **S1（确认）；task-62 已修复（2026-10-28）：7 导出恢复 + `tools/verify_jni_exports.py`，见 review §8.6** |
| N02 | spawn file_actions：`adddup2` 之后 `addclose(control_pair[0]/[1]/raw_fd)`，未排除原 fd==3/4；碰撞会关闭刚安装的 3/4 | victim_protocol.c:405-425 | addclose 前判原 fd≠3/4（或按 I5 先 close 原 fd≠3/4 后 dup2） | S2 |
| N03 | helper perf raw 拷贝无 acquire 屏障：volatile 读 data_head 后直接 memcpy（perf.c 有 fence） | victim_helper.c:391-420 | 用 acquire load+fence，tail 写 release | S2 |
| N04 | J helper perf 参数仅 10 字段；未传 exclude_user/hv/idle/attrDisabled/autoEnable（debug 有这些字段），采样语义与 legacy/R3e 不一致 | VictimProtocol.kt:158-169；victim_helper.c:333-370；A301SoDebugConfig.kt:1079-1105 | 扩字段/协议 v1 兼容位或 Kotlin 校验+fail-closed；加向量 | S2 |
| N05 | app 侧 victim 写无 SIGPIPE 防护（helper 死时 write 可能终止 app；helper/legacy 均 SIG_IGN） | victim_protocol.c:171-193；jni43499.c（无 signal）；对照 helper:841、legacy backend:431 | app JNI_OnLoad `signal(SIGPIPE,SIG_IGN)` 或 `send(MSG_NOSIGNAL)` | S2 |
| N06 | `glk_victim_kill` 硬编码 2000ms 等待界（A 决策无 Native 默认/硬编码） | victim_protocol.c:618 | 增加 timeout 参数或用 Kotlin closeTimeout | S2 |
| N07 | 单 W1 Flow 在 W1 前启动 victim（:227 早于 :229 placement）；E1 W1-only 若走该路径受 victim 启动影响 | Flow43499Impl.kt:216-232 | 单 W1 也把 victim start 移到 W1 后，或 E1 用 JNI 直调 harness | S2 |
| N08 | helper 继承 app environ（spawn envp=NULL），execScript 脚本再继承该环境（设计要求最小 env） | jni43499.c:1046-1052；victim_protocol.c:427-431；helper:600 | Kotlin 传显式最小 env 或 helper 设 PATH=/system/bin | S3 |
| N09 | raw 窗口边界仅按请求校验，Native 未交叉校验 raw_capacity 与 ring 数据区（Kotlin 已算 ring+16） | victim_protocol.c:507-531；helper:250-290 | 文档化 Kotlin 唯一权威；Native 只校验已有域（无需新增策略） | S3 |
| N10 | debug 默认 `injection.victim=process`+`perf.enabled=0`；J 测试若忘改配置会静默走 ProcessVictim | debug-a301so.conf:115-120；A301SoDebugConfig.kt:551-552 | E1/E5 配置断言 jni-spawn+perf.enabled=1；启动检查 | S3 |

## 7. 快照/UNVERIFIED/物证缺口

1. **快照**：写者 r3e-impl 已停手（inactive），`jni43499.c` hash=`d5353842…` 两次采样不变 ⇒ N01=确定性 S1；行号按此快照复核。
2. **未验证**：E1/E2/E2b/E3/E5 未跑；E0/E4 据 [perf-e0-e4-20261027](device-gates/perf-e0-e4-20261027.md) PASS（fd={0..4}、HWM 3004KB、execScript、timeout/EOF/detach、fork_imports=0；①wrap 计数=task-56 待门禁），本矩阵未复跑。
3. **source-only 行**：全部 syscall 行未真机验证；pstore/panic/冷机重复（⑥）未覆盖；三 route 命中率未知（G1 仅 perf 权限）。
4. **legacy 对照**：L2 多入口 `.disabled`（无运行证据）；L1/VL 源码语义对照，行号以只读树当前文件为准。
5. **合并**：四路审计原文未复制；本矩阵引用其阻断映射，J helper 部分首次成表。
