# 43499 kt-step 生产引擎计划（2026-10-20 · v3）

- 级别：L（攻击关键路径 + 跨 Native/Kotlin 契约 + 生命周期）
- 用户裁定（2026-10-20）：**所有逻辑在 Kotlin 解决；C++ 只保留最薄 JNI syscall 接口**；UI 层执行接线先不做（配置声明/获取不做）。
- 关系：本件是**唯一生产计划**；[43499-production-engine-plan.md](43499-production-engine-plan.md) 与 [43499-native-device-ops-plan.md](43499-native-device-ops-plan.md) 降 legacy 记录。
- 明确不做：UI/配置/profile、43284、64560、Lua/CLI、旧 C++ 逻辑的生产调用。

## 0. 裁定与两处必须批准的偏离

`docs/analysis/jni-six-targets-params-plan.md:58-65` 已记录：**跨 JNI 的每次 futex 可能破坏 PI 窗口时序**。因此“每个 futex 一次 JNI”不能直接作为生产方案，除非真机证明安全。本计划把边界拆成两层：

1. **非 PI 路径**：逐 syscall JNI 薄包装（mmap/perf/socket/close/…），Kotlin 逐次调用；
2. **PI 关键路径**：**actor-level syscall batch**（`gl43499_waiter/owner/consumer`），C++ 只执行 syscall 顺序与返回原始计数器；attempt 次数、target/value、limits、成功判据、重试、收尾全部在 Kotlin。C++ 不做 guard/judge/retry/策略。

**批准状态（逐项以 [43499-approval-status.md](43499-approval-status.md) 为唯一权威）**：**原则＝已批准**；**生产启用＝仅第 1 层（逐 syscall）**。第 2 层 actor batch / victim child helper：原则已批准、**生产启用待批准**，当前不生效 ⇒ K3/K4 不进生产；K1/K2 只依赖第 1 层。**不得读成「生产已批准」**。

## 1. 目标与判据

把 43499 的步骤编排、路由决策、页/凭据布局、目标选择、准备/发现、验证与收尾全部放进 Kotlin；C++ 只保留逐 syscall 包装与上述 actor/child syscall 循环。

- **K1 判据**：Kotlin `KernelSyscalls` + `NativeSyscalls43499` + C++ 薄库可编译；fake 覆盖每个函数的成功/errno/超时/关闭；host syscall smoke 通过；Android 零告警。
- **K3 判据（待批准后）**：Kotlin 三演员 + actor batch 在真机跑出与基线同形的 `requeue ret/errno`、`calls/successes`、`route_done`；冷机、固定 CPU 对、单 route、KernelSU 未加载。
- **K5 判据（待批准后）**：`victim uid=0`；K5 的非目标（seccomp/leaf/handoff）单列技术债。

## 2. 现状与冲突

| 事实 | 位置 | 结论 |
|---|---|---|
| K1 Kotlin 引擎经 `JniBackend43499Port` 调 C++ `Native43499` | `app/.../engine/kernelmemory/`；`core/src/boundary/jni/native_43499.cpp` | 不满足“C++ 仅 syscall”；K1 降 legacy |
| 旧 C++ 43499 含 `KernelAccess`/`MulticastRoute`/prepare/discover | `core/src/backend/cve_2026_43499/**` | 必须从生产构建/APK 隔离（K0） |
| N1b 把 form 逻辑加进 C++ `WriteRequest` 链 | `kernel_access.hpp` 等 | 降 legacy；新路径不调用 |
| 旧 `libghostlock43499.so` 仍进 APK | `build.gradle.kts:296` | K0 必须移除/隔离并用 `nm`/APK 检查 |
| PI 时序约束 | `jni-six:58-65`；`pi-window-constraints.md` | 采用 §0 两层边界；K0 PI 门禁未过不称 production |

## 3. 目标结构

```text
KtStepEngine（Kotlin，生产）
  ├─ StepEngine + PlanCatalog（既有 Kotlin）
  ├─ KernelLayout（Kotlin：页/凭据/首指令/分支编码）
  ├─ FutexRoute（Kotlin：attempt 策略、target/value、actor 线程生命周期）
  ├─ Prepare/Discover（Kotlin：泄漏/页准备决策）
  ├─ VictimPlan（Kotlin：命令/超时/清理；child 由 C++ syscall helper 执行）
  └─ KernelSyscalls（Kotlin 接口）
       ├─ NativeSyscalls43499（唯一 JNI）
       └─ FakeKernelSyscalls（host 测试）

C++ libgl43499syscalls.so：
  - K1 逐 syscall：futex/mmap/munmap/socket/setsockopt/send/recv/memfd_create/
    perf_event_open/ioctl/gettid/getpid/close/usleep/clock_gettime/pipe2/read/write/
    sched_setaffinity/sched_setattr/sched_getcpu/rseq；
  - PI actor batch：waiter/owner/consumer（仅 syscall 顺序 + 计数器；待批准）；
  - K4 victim child helper（fork/execve/wait4/kill/prctl/setpgid/openat/fcntl/dup2/getuid；待批准）。
```

**不变量**：C++ 不 include 路由/页/会话逻辑；Kotlin 不调用旧 `Native43499`；所有资源 owner 在 Kotlin；C++ 无全局可变状态。

### 3.1 Kotlin actor 线程设计（A4）

- 每次 attempt 由 Kotlin 创建/复用一个 waiter、一个 owner、一个 consumer 线程；线程入口只调用 `KernelSyscalls` 或 actor batch，不读 profile、不选 route。
- 线程 affinity 用 `sched_setaffinity`（Kotlin 调用；主/消费者 CPU 来自输入）；`sched_setattr` 只在 PI 外调用；PI 内零分配/零打印/零间接分派。
- JNI attach：线程启动时 attach，退出时 detach；JNI 包装不分配、不缓存局部引用；actor batch 返回后不再访问 Kotlin 缓冲。
- 共享状态用 Kotlin `AtomicInteger`/`AtomicReference` 或直接 `mmap` 的 ByteBuffer；PI 内的读写不用 Kotlin 集合/字符串/装箱。
- 取消/失败：Kotlin 置 stop 标志 → 唤醒/join → 关闭 fd/mapping；所有出口同一 `finally`。
- 静态检查（K1/K2 门禁）：PI 段 Kotlin 文件禁 `String`/`List`/`Map`/`println`/`Log`/`new` 的扫描；JNI 白名单；`sched_setattr` 调用点必须在 PI 外锚点之后。

## 4. PI 粒度与 K0 门禁（拆成 K0a/K0b）

- **K0a（先于 K1）**：legacy 隔离 + 构建/符号/APK 检查；不跑真机。
- **K1（先于 K0b）**：逐 syscall 契约/薄库；K0b 需要它的 futex/socket/close 包装。
- **K0b（K1 之后、K3 之前；K2 不依赖它）**：最小 actor-batch PI spike（一次 attempt，不是生产路由）。记录 `dmesg`、`route_done`、`calls/successes`、`requeue ret/errno`；失败则停，K3 不进入生产，回用户裁定。
- K1/K2 是离线原型；K0b 通过后才可继续 K3。PI 内禁 `sched_setattr`/`nice`/`setpriority`、分配、打印、间接分派；`sched_setattr` 只在 PI 外由 Kotlin 调用。

## 5. 批次

| 批 | 内容 | 门禁 |
|---|---|---|
| K0a | **已实现（APK 隔离）**：移除 `prepareGhostlockJniLibs` 的旧库复制；`packaging.jniLibs` 排除 `libghostlock43499.so`；移除 `KernelMemoryEngine.production()`；APK 检查只留 `gl43499syscalls`。CMake 旧 target 仍构建（legacy 不打包）；`verifyJniLibs` 的 43284 缺库是既有缺口 | APK `unzip -l` 检查通过 |
| K1 | **已实现**：Kotlin `KernelSyscalls`/`NativeSyscalls43499`/`NativeKernelSyscalls`/`FakeKernelSyscalls`；C++ `syscalls_43499` + `native_syscalls_43499` + `gl43499syscalls`；host 实测 + Kotlin fake | `nativeTestHost` **76/76**、`nativeBuildAndroid` 零告警、Kotlin 3/3；见 §11 |
| K2 | **已实现**：Kotlin `KernelLayout`（页/凭据/布局/arm/编码）+ `KtStepEngine`/`KtOpsInvoker`/`KernelRoute` 骨架（StepEngine 顺序/重试/冻结）；页序修正为 node 后写 task/lock | host 单测 **16/16**（布局 7 + 引擎 9）；字段向量与 C++ 实现语义对拍 |
| K0b | 最小 actor-batch PI spike，验证逐 syscall/actor 粒度 | **PI 探针 PASS（AVD 2026-10-20）**：`requeue=1`/`wait=0`；`page/root=false`，不标 supported |
| K3 | **部分实现（离线）**：`KotlinPiRace`/`FutexWord` + `SyscallFutexWord`/`KernelFutexWords`/`PiSpike` + `KotlinPiActors` 三线程骨架 + `PiActorsKernelRoute`/`PiProbeRunner` 诊断 + `PageWriter`/`K3PageFixture` 边界 + uid 校验（33 测）；route/page 写入未做；C++ actor batch 待批准 | 真机：冷机、固定 CPU 对、单 route；固定读数 |
| K4 | **待用户批准**：Kotlin prepare/discover/victim 编排；C++ victim child helper（fork/pipe/probe/kill/wait） | 真机：prepare/discover 有值；probe uid/comm；child 全出口关闭 |
| K5 | 端到端 `victim uid=0` + 收尾；归档 | `docs/analysis/device-gates/43499-kt-step-*.md` ≤8 KB |

**K1 文件清单**：

| 文件 | 内容 |
|---|---|
| `app/src/main/kotlin/com/ghostlock/app/engine/ktstep/KernelSyscalls.kt` | 接口、结果、errno、参数校验 |
| `.../NativeSyscalls43499.kt` | 唯一 `external fun` 面 |
| `.../FakeKernelSyscalls.kt`（test） | 记录调用、可脚本化 errno/返回值 |
| `core/src/boundary/jni/native_syscalls_43499.cpp/.hpp` | 逐 syscall 包装；无状态/无决策 |
| `core/src/tests/syscalls_43499_test.cpp` | host 实测：futex 用户字（0/EAGAIN/EDEADLK/EINTR/EFAULT）、mmap/munmap（长度 0/ENOMEM/重复）、socketpair/pipe2/read/write/close（EBADF/重复）、perf_event_open（EPERM/ENOSYS）、ioctl（EINVAL）、gettid/clock_gettime |
| `core/CMakeLists.txt` | `gl43499syscalls` SHARED；旧 `ghostlock43499` 默认 OFF |

**syscall 覆盖矩阵（K1 必须逐项列出，不用“18 个”概括）**：

| 类别 | 函数 | 测试 |
|---|---|---|
| 内存 | mmap/munmap/memfd_create | 成功/负 errno/长度 0/重复 munmap |
| socket | socket/setsockopt/send/recv/close | 成功/EBADF/EINTR/超时/重复 close |
| futex（非 PI 测试） | futex(op, addr, val, timeout, addr2, val3) | 0/EAGAIN/EDEADLK/EINTR/EFAULT |
| perf | perf_event_open/ioctl | 成功/ENOSYS/EINVAL/关闭 |
| 线程/时钟 | gettid/getpid/clock_gettime/usleep/sched_getcpu/sched_setaffinity/sched_setattr/rseq | 成功/errno/边界 |
| 管道（K1） | pipe2/read/write/close | 成功/EOF/EBADF/重复 close |
| victim（K4，待批准） | fork/execve/wait4/kill/prctl/setpgid/openat/fcntl/dup2/getuid | 成功/EOF/超时/ESRCH/重复 wait；child 不返回 JVM |

## 6. 所有权与生命周期

| 对象 | 创建 | 所有者 | 终结点 | 释放者 |
|---|---|---|---|---|
| 映射 | Kotlin `mmap` | Kotlin | `munmap` | Kotlin |
| fd/socket/pipe | Kotlin | Kotlin | `close` | Kotlin |
| actor 线程 | Kotlin | Kotlin | join | Kotlin |
| C++ actor/child 调用 | Kotlin | Kotlin | 返回/join/wait | Kotlin |
| syscall 端口 | 构造 | 引擎 | 无状态 | — |

**不变量**：C++ 不缓存 fd/指针；Kotlin 每个资源 finally close；失败/取消/超时同序；actor batch 返回后不再访问 Kotlin 缓冲。

## 7. 关键选择（CBN）

| 决策点 | 备选 | 收益 | 成本/边际 | 翻转条件 | 结论 |
|---|---|---|---|---|---|
| PI 粒度 | A 每 syscall JNI；B actor batch | A 逻辑最纯；B 时序稳 | A 每演员每轮多次 JNI；B 每演员 +1 函数 +1 批处理分支；每新增 actor 参数 +1 字段 | K0b 真机证明 A 安全 | A（K1/K2 先做；B 待批准） |
| victim | A Kotlin fork；B C++ child helper | A 逻辑全 Kotlin；B 子进程安全 | A fork 后 JVM 不安全；B +1 helper +1 协议 + fork/exec 生命周期 | 用户批准且证明 fork 安全 | A（K1/K2 不 fork；B 待批准） |
| legacy | A 立即删；B 隔离保留 | A 树干净；B 可回滚 | A 删 20+ 文件；B 0 删除 + 构建隔离 + 恢复清单 | K5 后清理 | B（K0a 隔离） |
| golden | A 旧 C++ 生成；B 固定 fixture | A 0 新 fixture；B Kotlin 单权威 | A C++ 仍是权威；B 1 fixture + sha256 + 漂移测试 | 旧 C++ 删除后 | B |

**逐批回滚（A7）**：K0a = 恢复 CMake/build 行与恢复清单；K1 = 删除新 Kotlin/C++ 文件；K0b/K3/K4 = 停批、保留 legacy C++ 不接入生产；K5 失败 = 回 K3 前的 fail-closed 状态，不半接线。每批 owner = Lead + 对应实现者；用户批准点 = actor batch、child helper。

## 8. 验证矩阵

| 层 | 输入 | 预期 |
|---|---|---|
| host fake | 每个 syscall 成功/每个 errno/超时/重复 close | Kotlin 只翻译，不吞 errno；资源计数为 0 |
| host syscall | futex 用户字、匿名 mmap/munmap、gettid、clock_gettime | 返回值/errno 与 libc 一致 |
| Android | `nativeBuildAndroid` | 零告警；导出符号与 Kotlin external 一一对应 |
| K0a | CMake OFF/APK/`nm` | 旧逻辑符号 0；新库符号与 Kotlin external 一致 |
| K0b 真机 | 最小 actor spike | `dmesg` 无 panic；`route_done`/计数与基线同形；未过则停 |
| K3 真机 | Kotlin actors + batch | `requeue ret=-1 errno=35`（或基线同形）、calls/successes、route_done 固定值 |
| K5 真机 | 全链 | `victim uid=0`；seccomp/leaf/handoff 列非目标/技术债 |

## 9. 明确保留

- 不改 UI/配置/profile；不改 43284/64560。
- 旧 C++ 43499 逻辑不删，K0 从生产构建/APK 隔离；恢复清单 `git show 92d55d3e:<path>`。
- K1–K2 不跑真机；K0/K3 起真机门禁。
- 不新增 Lua/CLI/临时旁路。

## 10. 风险

| # | 风险 | 处置 |
|---|---|---|
| R1 | 逐 syscall JNI 破坏 PI 时序 | K0/K3 真机门禁；选 actor batch；失败即停回用户 |
| R2 | Kotlin 线程/GC/safepoint 影响窗口 | PI 内零分配/零打印；静态检查；K3 冷机复跑 |
| R3 | fork 子进程回到 JVM | C++ child helper；父/子 fd、超时、kill/wait、失败清理 |
| R4 | 旧 C++ 仍被误调/进 APK | K0 构建/符号/APK 检查；`KernelMemoryEngine.production()` 移除 |
| R5 | golden 漂移 | 固定 fixture + 哈希；Kotlin 唯一生产实现 |
| R6 | 用户只批准逐 syscall、不批准 actor/child | 计划停在 K1/K2；K3/K4 不进入生产 |

## 11. K1 实现状态（2026-10-20）

- Kotlin：`app/src/main/kotlin/com/ghostlock/app/engine/ktstep/`（`KernelSyscalls.kt`、`NativeSyscalls43499.kt`）。
- C++：`core/src/platform/syscalls_43499.{hpp,cpp}`、`core/src/boundary/jni/native_syscalls_43499.cpp`、`core/src/tests/syscalls_43499_test.cpp`；CMake `gl43499syscalls` + `syscalls_43499_test`。
- 证据：`nativeTestHost` **76/76**；`nativeBuildAndroid` 零告警；`libgl43499syscalls.so` 导出 24 个 JNI 符号与 Kotlin external 一一对应；Kotlin `KernelSyscallsTest` **3/3**。
- K0a 已做 APK 隔离：`unzip -l` 显示 APK 只有 `libgl43499syscalls.so`，无 `libghostlock43499.so`；旧 CMake target 仍构建但不打包。
- **K0b = PASS（AVD）**：`requeue=1`/`wait=0`，但 `page-write=false`/`root=false` ⇒ **不是生产证据、不标 supported**（`device-gates/k0b-probe-20261020-0952-pass.md`）。
- 未做/待批：actor batch 与 child helper 的**生产启用**；`verifyJniLibs` 既有红。

## 13. K3.1 PI 决策移植（2026-10-20）

- `KotlinPiRace`/`FutexWord`：移植 C++ `PiRace` 的 lock→cmp≥1→Running→disarm 顺序；不涉及线程/页/路由。
- 测试：`KotlinPiRaceTest` 4。

## 14. K3.2 futex 桥（2026-10-20）

- `SyscallFutexWord`：lock/wait/cmp/unlock 四个 PI op 桥到 `KernelSyscalls.futex`，返回值和 errno 原样映射。
- `KernelFutexWords`：`mmap` 一页、chain/target 两字、`munmap` 释放；失败具名 errno。
- `PiSpike`：一次 `KotlinPiRace` + 映射创建/关闭；不启动线程、不碰页/路由。
- 测试：`FutexWordBridgeTest` 5。

## 15. K3.3 三演员骨架（2026-10-20）

- `KotlinPiActors`：waiter（chain lock/park）、owner（target lock/requeue gate）、requeue（cmp）三条 Kotlin 线程 + latch；可配 join 超时，超时中断并标记。
- 测试：`KotlinPiActorsTest` 4（happy、zero requeue、owner lock fail、stuck timeout）。

## 16. K3.4 victim uid 校验（2026-10-20）

- `KtStepEngine` 现在要求注入 `VictimProbe`；三步全 Ran 后再读 uid，只有 `uid == expectedUid`（默认 0）才 `Succeeded`。
- 失败具名：`probe:<errno>` / `victim-uid:<uid>`；不再把“route 返回 Landed”当成 root。
- 测试：引擎 12 + route 3 + K0b 3 + 分离 wait 1；合计 **70/70**。
- `PiActorsKernelRoute` 跑三演员但永不返回 `Landed`，只 `Indeterminate`/`Refused`；防误报成功。
- 真实 probe 仍待 K4 victim pipe；当前测试用 `FakeVictimProbe`。

## 12. K2 实现状态（2026-10-20）

- Kotlin：`app/src/main/kotlin/com/ghostlock/app/engine/ktstep/KernelLayout.kt`（`writeLayout`/`layoutMatchesRequest`/`layoutAcceptsPage`/`armFor`/`encodeWaiterArm`/`buildPayloadPage`/`buildCredentialImage`）。
- 测试：`KernelLayoutTest` 4 + `KernelCredentialTest` 3 + `KtStepEngineTest` 9；字段向量取自 C++ `payload_page_test` 的 retired inputs，但修正了其过期的 arm 期望，并把 node 写序放在 task/lock 之前（否则 0x30/0x38 被 node 覆盖）。
- `KtStepEngine` 用既有 `StepEngine` 管顺序/重试/冻结；`KtOpsInvoker` 每步做 Kotlin 布局→页→`KernelRoute.attempt`；`KernelRoute` 是待 K3 actor batch 填的实现边界。
- 证据：`./gradlew :app:testDebugUnitTest --tests 'com.ghostlock.app.engine.ktstep.*'` = **19/19**（K1 3 + K2 16）；Kotlin 编译零告警。
- 未做：把 C++ `payload_page.cpp` 的写序 bug 回写到 legacy（不接入生产）；golden 二进制 fixture 待 K3 前固定。
