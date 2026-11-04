# 43499 纯 app 域完整攻击执行模型 L 设计 v4（JNI 主路径 + posix_spawn 通用 victim）

- 状态：**L 设计 v4（修订 v3，回应 F1/F2/F3+I1–I4），待三轮复核**；零实现（不改代码、不 commit）。**U1–U3 批准、U4 确认**（approval §6.8）。
- 日期 2026-10-27；作者 routes-impl2；快照 HEAD `a9af5482`+工作树（109 单测绿、`perf.enabled=0`）。
- 硬约束（用户）：①JNI 重点、不可用 exec 整体代替；②不用 fork、用 posix_spawn；③victim 拆为**通用 victim**。v4=JNI 主路径+posix_spawn backend-agnostic helper；生产/JNI **零 fork**；exec 仅 helper 启动/finish 脚本、**不作攻击载体**。v2 作废、v1/v3 已修订。
- critic 响应：F1→§5、F2→§7 E4、F3→§8.5 E5/P0c、F4→§8、F5→§6；I1→§7 E2b、I2→§5.3、I3→§8.4、I4→§8.5。
- 依据：G1、perf plan、三路 review、approval §6.6–6.8、gates。

## 0. 结论摘要

1. **模型 J（U1 已批）**：Kotlin→JNI W1（无 perf）→permissive→**JNI 调通用 victim：posix_spawn nativeLibraryDir helper（不 fork）**；helper 只承载 victim+perf self-sample(pid=0)+finish/park→raw 经 memfd→JNI `jbyteArray`→**Kotlin 解析/投票/门槛/选址**→JNI W2/W3→helper 执行 finish 脚本→park。
2. **通用 victim**：Kotlin `core/victim/`+Native `core/victim_protocol`+helper PIE；能力集 backend 声明；43499 只作使用方；43284 复用见 §8.4。
3. **零 fork/性能**：生产/JNI 禁 `fork()`；victim 一律 posix_spawn（CLONE_VM|CLONE_VFORK，不复制 JVM 地址空间）；E4 数值判据+**零 fork 证明**（wrap=0+`readelf` 无 fork/vfork 符号+调用图审计+tracepoint 基线）。
4. **F1 fd**：control=3/raw=4；`close_range(max(3,4)+1,~0)`（或 `POSIX_SPAWN_CLOEXEC_DEFAULT`+显式 dup2）；spawn 前/后 fd 清单、inherited-fd 探测、**泄漏=0**；`execScript` 同规则；file_actions 完整（§5.2）。
5. **I2 malloc**：helper 是 posix_spawn **exec 后新单线程进程**（无 JVM/无 fork 复制）⇒**允许 libc malloc**；perf.c calloc/free 纳入 E4/单测；"无 malloc"仅限 JNI/app 侧 fork 语义。
6. **F3/E5**：E5 进 §8.5 P0c（J 后、生产启用前），step=W1→helper self→Kotlin 解析/投票→JNI W2/W3→finish→enforce 恢复，依赖 E1–E4。E1=W1-only JNI；E2=W1 permissive 后 helper self-perf（决定性）；**E2b=冷启动未 W1 G1 基线**；E3=self vs cross；E4=posix_spawn 可行性/性能/零 fork。
7. 逻辑全 Kotlin（F5，§6）；perf 仅机制；`perf.enabled=0`；**未发现 JNI+posix_spawn+通用 victim 的不可解冲突**。

## 1. legacy 参考模型（攻击链可纯 app 域完成；载体语义仅参考，非 JNI 证据）

共享模型：app 域→W1（无 perf `very_legacy/main.c:1294-1313`）→permissive（`main.c:1166`）→victim（同 uid/域/seccomp，`main.c:1076-1090`；legacy `victim_process.cpp:192`）→victim 内 perf self-sample(pid=0)（`main.c:863`；`legacy/attack/ops.cpp:434-513`:448；ABI 1/2 :892/:484）→W2/W3（`main.c:1629-1656,1478`、`cve_2026_43499_backend.cpp:122-128`）→rooted victim exec root script（`main.c:1044-1056`、`victim_process.cpp:136-154`、`root_child_frontend.cpp:26-60`）。入口：very_legacy=CLI/同进程 JNI（`jni_exploit.c:8-33`、`glk43499_api.c:28-30`）；legacy=CLI/app exec（`legacy/main.cpp:29-128,44`；历史 Kotlin `git show 1e65cea8:…:620-720`）；legacy2=2.0.8 同进程 JNI 分步 W1（`git show bb27b48a:…/jni43499.c:28-146`），当前 `.disabled`。**保留攻击链语义；载体=JNI+通用 victim helper（不 exec 整体、不 fork）。**

## 2. 历史物证（均 exec/CLI 载体=参考，非 JNI 证据；前缀 `archive/device-gates/`）

| 日志/构建 | 域/uid | seccomp | KSU 起点 | 结果 | 证据（仓库相对路径:行） |
| --- | --- | --- | --- | --- | --- |
| U01-B（163）09-13 | untrusted_app 10507 | filters=1 | 未加载 | 全链 PASS uid=0+KSU | `…U01-B-20260913-direct-pass.native.log:47-48,83,106-107,146,204-205` |
| CPP08（206）09-17 | untrusted_app 10510 | filters=1 | 未加载 | 全链 PASS | `…CPP08-20260917-direct-pass.native.log:48-49,101-104,143,189-208` |
| CPP12h（236）09-17 | untrusted_app 10510 | filters=1 | 未加载 | 6/6 route PASS | `…CPP12h-20260917-multicast-pass.native.log:47-48,100-104,142,192` |
| G1 10-27 | untrusted_app 10672 | — | 未加载 | perf EACCES（Enforcing、无 W1） | `docs/analysis/device-gates/perf-g1-20261027.md:34-44` |

**U4 已确认**（用户：三个 legacy 均有完整攻击物证）；仓库内 exec/CLI 存档仅上述日志，**JNI+helper 形态零 app 域门禁**（E1/E2/E5 首证）；启动/paranoid/ABI/build/KSU 记录缺口不阻塞 J 实现。

## 3. G1 一致性（H1 最可能；U3 已批）

H1 时机/Enforcing：Enforcing 拒→W1 permissive 允许（E2 判；历史 U01-B:83→:106）；H2 seccomp（permissive 后仍拒且 errno；历史 filters=1 下 perf OK⇒反证）；H3 ptrace/Yama 只影响跨任务（helper self 规避，E3）；H4 ABI=2 parser 拒⇒假失败（I1 前置）。G1 Enforcing FAIL=预期。

## 4. 模型选择（exec 不作攻击载体；零 fork）

| 模型 | 载体 | perf 位置 | 结论 |
| --- | --- | --- | --- |
| **J（首选，U1 已批）** | app JNI（W1/W2/W3）+posix_spawn victim helper | helper 内 self-sample；raw→memfd→app | 采纳：JNI 重点+无 fork；复用 JNI/Flow/解析投票；成本=通用 victim+helper 打包 |
| D 现状跨任务 JNI | app JNI 采样 Kotlin `sh` victim | app 跨任务 | 被 J 取代；解析/写入保留 |
| C 非 perf 替代 | app 域 | 不用 perf（43284 取内核读写再定位） | 兜底（E2/E3 证伪且用户裁定） |
| A/B exec 整体载体、fork-from-JVM | — | — | **排除**（生产/JNI 零 fork；exec 不作整体载体） |

## 5. F1：JNI + posix_spawn 机制闭合

### 5.1 通用 victim 边界与能力集

- Kotlin `core/victim/`（backend-agnostic、禁 import backend）：进程/会话/协议/结果/capabilities/identity；Native `core/victim_protocol`：posix_spawn/file_actions/framing/raw relay/timeout/waitpid/park/bye/detach；helper 机制-only。
- **能力集（backend 声明启用）**：`probeUid`、`spawnHelper`(posix_spawn)、`perfSelfSample`(仅 raw；解析/投票 Kotlin)、`execScript(path,argv,env)`(当前 uid；rooted 后 finish)、`park/bye/detachRooted`；未启用命令⇒具名拒绝。
- 43499 使用方：`backend/cve43499/victim/` 适配器映射通用 session→`Cve43499WriteTargetSource`/baseline/finish；Flow/lifecycle 只依赖通用接口。

### 5.2 helper 打包、posix_spawn、fd 协议（F1）

| 项 | 设计 |
| --- | --- |
| 构建/打包 | helper 独立 PIE（机制-only、无 43499 语义）→`nativeLibraryDir/libghostlock_victim_helper.so`（`build.gradle.kts:166-172`；同类实证 `CMakeLists.txt:40`） |
| spawn/域 | app `posix_spawn(nativeLibraryDir/helper, argv, env, file_actions)`；子进程保持 app uid/域/seccomp（E4 记录；权限位 AGP/PMS 保证） |
| **fd 协议** | **control=3/raw=4（协议 ABI）**；父建 pipe/socketpair(control)+memfd(raw)，`adddup2`→3/4；child fd 集={0,1,2,3,4}；helper 启动 `close_range(max(3,4)+1,~0)` 关其余继承 fd（或 `POSIX_SPAWN_CLOEXEC_DEFAULT`+显式 dup2，实现批冻结） |
| file_actions | init→adddup2(control,3)→adddup2(raw,4)→addclose(原 fd)(可选)→destroy；失败清理已建 fd；spawn 前/后 fd 清单入 fixture |
| 测试/泄漏 | helper 启动自检 fd≥5 `fcntl(F_GETFD)`=EBADF（inherited-fd）；**fd 泄漏=0**（spawn/close 后 `/proc/self/fd` 计数差=0）；app 同检 |
| `execScript` | helper 内 posix_spawn(脚本) 复用同 file_actions/close 规则（仅 0/1/2）；失败码机械回执 |
| 父侧/禁止 | Native 持 pid/control/raw；waitpid+超时、EOF/退出/panic⇒fail-closed（pstore）；park=不杀、detachRooted=只 detach、bye/close=kill+reap；生产/JNI 禁 fork |

### 5.3 helper 内部（机制-only；I2 malloc 澄清）

**I2**：helper 是 posix_spawn **exec 后新单线程进程**（无 JVM/无 fork 复制）⇒**允许 libc malloc/calloc/free**；perf.c calloc/free 入 E4/单测（成对、不泄漏）；v3"无 malloc"仅限 JNI/app 侧 fork 语义。启动：关多余 fd→读 control 帧→单步机制：`perfSelfSample`（perf_event_open(pid=0,cpu=-1,attr 全参)→mmap ring→ioctl ENABLE→Kotlin 指定 N×getpid→DISABLE→raw 写 memfd→status 回执）、`probeUid`、`execScript`(rooted 后 posix_spawn 脚本)、`park`(futex wait)、`bye`。**禁止**：解析/选择/投票/门槛/选址/重试/循环策略/日志决策/默认值/JNI/网络/线程；pass 数/pump N/attr/raw_len/超时全由 Kotlin 下发。

### 5.4 raw 传输（256 KiB；memfd/pipe）

app 预建 raw memfd（`passes×raw_len`、ring_pages 由 Kotlin 给；数据区历史 64 页=256 KiB，page size 系统派生）；helper 每 pass `memcpy` raw→memfd 偏移并写 status（head/tail/raw_len/errno/pass 号）；app Native 等完成后读 memfd→JNI `NewByteArray+SetByteArrayRegion` 回 `jbyteArray`（分块 Kotlin 配置）；**Kotlin 切分/解析/SHA-256**；长度不符/短读/超时/EOF⇒fail-closed；pipe 仅 control。

### 5.5 finish/park/close

W2 rooted（uid=0）+W3 清 seccomp 后 Kotlin 发 `execScript`（仅脚本执行，不替代 JNI 攻击链）；helper 以当前 uid posix_spawn 脚本并回 exit/摘要；随后 `park`/`bye`；Kotlin `FinishAcceptance` 判定。close：停新命令→（必要时）kill→waitpid/reap→munmap/memfd close→释放 record；rooted park 只 detach（沿 task-45）；dirty/retained=1 至进程退出。

## 6. F5：逻辑边界

| 决策/状态 | 归属 |
| --- | --- |
| pump 迭代/计时、pass 组合、投票/门槛、选址/校验、SHA/evidence | Kotlin（复用 parser/target source seam） |
| W1/W1b/W2/W2b/W3 目标/模式/顺序/attempts/verify/失败策略 | Kotlin（Flow/WriteChain 不变） |
| victim 何时 spawn/sample/park/finish、finish 判定、baseline/precheck | Kotlin（43499 backend） |
| 通用 victim/Native | 进程生命周期、fd/pipe/memfd、帧 transport、raw 拷贝、posix_spawn/waitpid、机械回执；**零 43499 语义** |

## 7. F3：实验门（E1–E5；E2b/E4 数值判据）

| 实验 | 目的 | PASS | FAIL | 不可判定→下一步 |
| --- | --- | --- | --- | --- |
| E0 快照 | 域/uid/seccomp/paranoid/build/KSU/manager；冷机/固定 CPU/单 route | 记录齐全 | 缺项 | 补采重跑 |
| **E1 W1-only（JNI）** | W1 app 域成立（真 P0c） | W1 route clean+permissive 证据+无 panic+资源终结 | RouteMiss/Failed/panic | 复跑；再失败=J 阻断 |
| **E2 W1 permissive 后 helper self-perf（决定性）** | H1+J 采样可行 | self OK（280B/ABI=1/2/32 regs） | EACCES/EPERM | 仍拒→H2/seccomp；C 兜底（用户裁定） |
| **E2b Enforcing 对照（I1）** | 冷启动未 W1 基线 | boot uid/seccomp/enforce 快照；预期 EACCES（同 G1） | 若 OK⇒H1 重审 | G1 数据或重跑；与 E2 成对 |
| E2'（可选，U2 已批）root setenforce | 快速判 H1，仅实验 | self OK | 记录 errno | 清理 setenforce 1+冷重启；全程记录 |
| E3 self vs JNI 跨任务 | helper self 优于 cross | permissive self OK；cross 拒/不稳定即证 | permissive self 拒 | 记录 errno；J 采样位置重设计 |
| **E4 posix_spawn 可行性/性能/零 fork** | 见下阈值/证明 | 全部 PASS | 任一 FAIL | 打包/fd 修正；仍败→J 阻断 |
| **E5 JNI 全链 app 域复现** | §8.5 step 边界 | 各 step 全过 | 任一 FAIL | 归档 step+停/报 Lead |

**E4 数值阈值（初值，E4 冻结，Kotlin 参数）**：spawn **p50≤10 ms/p95≤30 ms**；helper `VmHWM≤64 MiB`；raw 256 KiB collect **≤50 ms（≥5 MiB/s）**、单命令超时≤2 s；**fd 泄漏=0**。**零 fork 证明**：①`--wrap=fork/vfork` 计数=0（回执上报）；②`readelf -Ws` 无 fork/vfork 未定义符号；③调用图审计无 fork(2)；④`sched:sched_process_fork` tracepoint 基线（区分 bionic posix_spawn 内部 clone/vfork，仅允许该来源；tracefs 需实验期 root，仅诊断）。E4 覆盖 perf.c calloc/free 成对与 helper malloc；无 tracefs⇒不可判定（只用①②③）。

E1 判据不依赖 app 读 enforce（W1 receipt/verify+现象+资源终结）；W1 后同一机制回写 enforcing=1 或强制冷重启；冷机/固定 CPU/KSU 未加载/快照/pstore/失败即停。fork 对照仅引用户观测代价，复测只在独立 host benchmark（用后删除），生产/JNI 永不 fork。

## 8. F4：迁移闭合（通用 victim/顺序/I3/I4）

### 8.1 接口/文件（实现批另批准）

Kotlin NEW：`core/victim/*`（6 类，backend-agnostic、无 43499 常量）；适配器 NEW：`backend/cve43499/victim/{Cve43499VictimAdapter,Cve43499JniVictimTargetSource}.kt`。Native NEW：`app/src/main/cpp/victim/{victim_protocol.c/.h,victim_helper.c}`+CMake（helper PIE+`gl_victim` STATIC）；JNI 入口 `spawnVictim/sampleVictim/victimIdentity/finishVictim/parkVictim/closeVictim`（append-only ABI、strict、无 reserved、参数 Kotlin）；W1/W1b/W2/W2b/W3 走既有 9 入口；helper 不 import backend，43499 只配 capability/argv/env/attr/raw。

### 8.2 归属/Flow 重排

`Cve43499VictimContext`（`Cve43499WriteChain.kt:126`）保留为身份基线（`victimIdentity` 填 pid/starttime/uid）；Flow 单写 `STEP_VICTIM_START` 移到 W1 后（写链 `Flow43499Impl.kt:350-351`；单写 `:202-204` 重排）；W2 precheck/finish verdict/`FinishAcceptance` 留 Kotlin；`Cve43499Lifecycle` 用通用 session close/park/detach。

### 8.3 task-45/46 迁移映射

| 现有 | v4 归属 |
| --- | --- |
| `ProcessVictim.kt`（Kotlin `sh`） | →`core/victim/VictimProcess`（posix_spawn helper）；旧文件实现批后「禁用而非删除」清理 |
| `VictimProtocol.kt` | →`core/victim/VictimProtocol`（帧 codec） |
| `VictimFinish.kt`/finish | →通用 `execScript`（posix_spawn 脚本） |
| rooted `park`/`detachRooted` | →通用 `park`/`detachRooted` |
| `core/perf.{h,c}` | 保留为通用 sampler Native 机制（仅 raw；calloc/free 入单测/E4）；跨任务 `Native43499PerfNative`/`Cve43499PerfTargetSource` **deprecate**（仅诊断，`perf.enabled=0`） |

### 8.4 通用性/测试/I1/I3

- **I3 43284 复用 checklist**：import 白名单（仅 libc/libc++/通用 victim 头；禁 backend/43499/glgeneral 私有）；capability mask↔命令映射；帧 schema（size/version/opcode）；argv/env；fd/raw 语义（3/4、memfd）；失败码；**禁用项**=禁 backend 业务命令。**依赖测试**：`core/victim` 无 backend import/无 43499 常量。
- 单测（不依赖 43499 常量）：帧 codec/坏帧/超时/EOF/错误码、raw 完整性（长度/截断/SHA）、rooted park、capability 禁用、fake helper/Flow seam、fd 泄漏=0、close_range/inherited-fd 探测。
- **I1 前置**：parser 接受 ABI=2+280B/8B regs 回归（+`sample_type=262145` 断言）先于 E2/E5（小实现批另批准）。

### 8.5 P0c 顺序（F3/E5）+技术债/I4

**P0c 顺序**：①I1 parser 小修→②E0/E1→③E2/E2b/E2'/E3/E4→④J 实现批→**⑤E5 JNI 全链复现（J 后、生产启用前）**→⑥W2/W3+finish 冷机门禁→⑦生产启用硬门（G1'）。**E5 step**：W1(JNI)→helper self-perf→Kotlin 解析/投票/选址→JNI W2/W3→finish→enforce 恢复；每 step 独立 PASS/FAIL，任一 FAIL 停并归档；依赖 E1–E4，失败不降级生产启用。
**技术债**：helper exec 许可待 E4；JNI app 域零历史物证；G1 门禁条件错（Lead）；legacy2 存档缺口（U4 已确认、后续补档）；旧跨任务清理（Lead）。**I4**：approval §6.8 正文保持冻结、后续只 append 行；需要拆附件时只按职责与权威边界处理，不设字节或篇幅门。

## 9. 不做 / UNVERIFIED

**不做**：不写实现；不改代码；不改 task-45/46 文件；生产/JNI 零 fork；exec 不作攻击整体载体；不改 legacy 三树；不 commit；不启用 `perf.enabled`；不宣称 supported。

**UNVERIFIED**：helper exec 许可/attr/uid/seccomp（E4）；permissive 后 self-perf（E2）；W1 app 域真机；raw 256 KiB 吞吐/超时；spawn p50/p95/RSS；零 fork 的 tracefs 可用性（不可判定用①②③）；finish；legacy2 存档。
