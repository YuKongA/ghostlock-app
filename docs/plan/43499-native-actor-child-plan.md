# 43499 native actor batch / child helper 计划（kt-step 扩展）

状态：**批准状态一律以 [43499-approval-status.md](43499-approval-status.md) 为准**（本文件不再单独声明批准）。
摘要：actor batch / child helper 的**原则**于 2026-10-20 获用户允许；**生产启用**仍为待批准——
[43499-kt-step-plan.md](43499-kt-step-plan.md) §0 只批准第 1 层（逐 syscall）。Kotlin 持有完整参数/判定，C++ 只执行。
本文件是 L 级计划，design-critic 通过前不写 N1 代码。

## 1. 范围

目标：K3 page landing、K4 victim child、K5 uid=0 的生产执行层；Kotlin 继续拥有参数、步骤、重试、uid 判据。
非目标：UI/配置/profile 读取；C++ 选择 route/重试/expected uid；删除 legacy；43284/64560。

## 2. 边界

| 对象 | 所有者 | 生命周期 |
|---|---|---|
| AttemptParams/ChildParams | Kotlin | 每次调用构造；C++ 只读 |
| page bytes | Kotlin | JNI 调用期间借用；join 后 Kotlin 才可释放 |
| native handle | Kotlin 持有，C++ 实现 | create → attempt/spawn → close；单 owner、close 幂等 |
| actor 线程组 | 单次 nativeAttempt | 三线程 join 后返回 |
| child pid/fd | native handle | spawn → command → finish/close；waitpid+close |
| 成功判据 | Kotlin | requeue/page_written/uid 由 Kotlin 解释 |

## 3. JNI 面

```text
nativeCreate(flags: Int): Long
nativeAttempt(handle: Long, params: ByteArray): Long   // result blob
nativeSpawnChild(handle: Long, childParams: ByteArray): Long
nativeChildCommand(handle: Long, cmd: Int, payload: ByteArray): Long
nativeChildFinish(handle: Long, action: Int): Long
nativeClose(handle: Long): Long
```

- `nativeAttempt` 一次调用内创建 waiter/owner/requeue 三线程、join、清理；不得 per-futex JNI。
- `nativeAttempt` 阻塞；Kotlin 取消不假设可中断；内部 join timeout 由参数给出。
- 热路径零 JNI 回调、零分配、零打印、零 profile 读取。

## 4. AttemptParams blob（v1，little-endian）

Header 112 bytes:

| off | size | type | 字段 | 约束 |
|---|---|---|---|---|
| 0 | 4 | u32 | magic `0x474C4E50` | 必须 |
| 4 | 2 | u16 | version=1 | 其它拒绝 |
| 6 | 2 | u16 | header_size=112 | 必须 |
| 8 | 4 | u32 | total_size | = page_offset+page_length |
| 12 | 4 | u32 | page_offset | 8 对齐，<= total |
| 16 | 4 | u32 | page_length | 1..0x10000 |
| 20 | 1 | u8 | pointer_bytes | 1..8 |
| 21 | 1 | u8 | flags | v1 必须 0 |
| 22 | 2 | u16 | reserved | 0 |
| 24 | 4 | u32 | chain_value | 参数 |
| 28 | 4 | u32 | wait_value | 参数 |
| 32 | 4 | u32 | target_value | 参数 |
| 36 | 8 | i64 | park_timeout_ns | -1 或 1..10s |
| 44 | 4 | u32 | join_timeout_ms | 1..10000 |
| 48 | 4 | u32 | settle_us | 0..1000000 |
| 52 | 8 | u64 | chain_addr | 非零 |
| 60 | 8 | u64 | wait_addr | 非零 |
| 68 | 8 | u64 | target_addr | 非零 |
| 76 | 8 | u64 | page_sink_addr | 非零 |
| 84 | 2 | u16 | waiter_cpu | 0..1023 |
| 86 | 2 | u16 | owner_cpu | 0..1023 |
| 88 | 2 | u16 | requeue_cpu | 0..1023 |
| 90 | 2 | u16 | reserved | 0 |
| 92 | 4 | i32 | waiter_nice | -20..19 |
| 96 | 4 | i32 | owner_nice | -20..19 |
| 100 | 4 | i32 | requeue_nice | -20..19 |
| 104 | 4 | u32 | policy | 0..6 |
| 108 | 4 | u32 | reserved | 0 |

page bytes 从 page_offset 开始，长度 page_length；`page_offset >= 112`，`total_size == page_offset + page_length` 且不得溢出；总长上限 0x10070。
短包、magic/版本/尺寸不符、page 与 header 重叠、page 越界、字段越界一律拒绝；C++ 不部分执行。

## 5. AttemptResult blob（v1，48 bytes）

| off | size | 字段 |
|---|---|---|
| 0 | 4 | magic `0x474C5253` |
| 4 | 2 | version=1 |
| 6 | 2 | reserved |
| 8 | 4 | waiter_result i32 |
| 12 | 4 | wait_result i32 |
| 16 | 4 | owner_result i32 |
| 20 | 4 | requeue_result i32 |
| 24 | 1 | actors_started u8 |
| 25 | 1 | join_timed_out u8 |
| 26 | 1 | page_stage u8（0 none/1 write/2 actor） |
| 27 | 1 | reserved |
| 28 | 4 | page_errno u32 |
| 32 | 4 | page_bytes_written u32 |
| 36 | 4 | reserved |
| 40 | 8 | elapsed_ns u64 |

结果只含原始计数/errno；不返回 success 布尔，不返回 uid 判定。

## 6. Kotlin 端口与接线

```kotlin
data class NativeAttemptParams(
    val chainAddress: Long, val waitAddress: Long, val targetAddress: Long,
    val pageSinkAddress: Long, val chainValue: Int, val waitValue: Int, val targetValue: Int,
    val page: ByteArray, val pointerBytes: Int, val parkTimeoutNs: Long,
    val joinTimeoutMs: Int, val settleUs: Int,
    val waiterCpu: Int, val ownerCpu: Int, val requeueCpu: Int,
    val waiterNice: Int, val ownerNice: Int, val requeueNice: Int, val policy: Int,
)

sealed interface NativeAttemptResult {
    data class Ok(
        val waiterResult: Int, val waitResult: Int, val ownerResult: Int, val requeueResult: Int,
        val actorsStarted: Boolean, val pageStage: Int, val pageErrno: Int, val pageBytesWritten: Int,
        val joinTimedOut: Boolean, val elapsedNs: Long,
    ) : NativeAttemptResult
    data class Errno(val code: Int) : NativeAttemptResult
    data class Malformed(val reason: String) : NativeAttemptResult
}

data class NativeChildParams(val argv: List<String>, val commandTimeoutMs: Int, val maxPayload: Int)

interface NativeAttemptPort {
    fun open(): SyscallResult
    fun attempt(params: NativeAttemptParams): NativeAttemptResult
    fun spawnChild(params: NativeChildParams): SyscallResult
    fun childCommand(cmd: Int, payload: ByteArray): SyscallResult
    fun childFinish(action: Int): SyscallResult
    fun close(): SyscallResult
}
```

`NativeKernelPageWriter(port, context)` 保存完整 `NativeAttemptParams` 模板；`write(sink,page)` 只替换 `pageSinkAddress=sink` 与 `page=page`，其余字段由调用方/Kotlin 提供，writer 不补默认值。

- `NativeKernelPageWriter : PageWriter`（kind=Production）只调用 `port.attempt`；`page_bytes_written != page.size` 即 `ShortWrote`/`Errno`。
- `NativeKernelPageRoute : ProductionRoute` 只在 `page_bytes_written==page.size && requeue>=1 && wait==0` 时 `Landed`；uid 仍由 `KtStepEngine` 判定。
- `NativeChildSession : VictimSession/ProductionProbe` 只调用 spawn/command/finish；uid 从 child 结果解析。
- 未注入 `NativeAttemptPort` 时 `NativeKernelPageWriter` fail-closed `ENOSYS`；默认 OFF。
- handle 由 `NativeAttemptPort.open` 创建，`close` 一次；page 在 `attempt` 返回前有效。

## 7. C++ 函数白名单

允许：
- `platform/syscalls_43499` 的全部薄 syscall；
- `landing/multicast_route_linux.hpp` 的 `lock_pi` / `wait_requeue_pi_until` / `cmp_requeue_pi` / `unlock_pi` / `pin` / `sleep_micros`；
- 新 `native_attempt_43499.cpp` 的 `run_attempt(params)`：actor 线程创建/join、参数校验、page copy、结果编码；
- 新 `native_child_43499.cpp` 的 spawn/command/finish/close；
- `posix_spawn`、pipe、waitpid、kill、fcntl 的薄包装。

禁止：
- `MulticastRouteConfig`、profile 读取、`MulticastRoute::judge()`、`execute()`、`requeue()`、retry 选择；
- `PayloadPage`/`build_payload_page`/`write_layout` 等 Kotlin 已负责的页/布局逻辑；
- C++ 内 expected uid、成功/失败判定、步骤顺序、重试预算。

N3 的 `run_attempt` 只执行 Kotlin 已建页和参数；若必须复用 legacy 某函数，先在计划中列 file:line 与输入输出，否则不调用。

## 8. 线程与 hot path

- `nativeAttempt` 在 PI 外创建/join 三线程；actor 线程内只做 futex/page copy，零 JNI/分配/打印。
- CPU pin/nice/policy 失败返回 errno，不重试；join timeout 到期后返回 `join_timed_out=1`，page 仍由 join 保护。
- 静态检查：actor 文件禁止 `JNIEnv`、`printf`、`new`、`std::vector`；host grep + Android 零告警。

## 9. child helper

- 选择 `posix_spawn`（不从多线程 JVM `fork`）：`/system/bin/sh` + 固定 loop；stdin/stdout pipe；stderr `/dev/null`；新 process group；fd CLOEXEC。
- 协议：`[u32 cmd][u32 len][payload]`；cmd=WAIT/EXEC/FINISH；payload 最大 4096；EXEC 为 shell command，child 只执行该命令。
- child 提权后只等待/执行 Kotlin payload/回报 uid/退出；不读 profile、不做 route、不回调 JVM。
- 父进程 waitpid WNOHANG + timeout + kill + close；handle 状态 EMPTY→SPAWNED→COMMAND→FINISHED→CLOSED，重复 close 安全。
- 测试：spawn/exec/uid/finish/超时/kill/重复 close；child 退出用 `_exit`/shell exit。

## 10. 验证与门禁

| 阶段 | 命令/判据 | 归档 |
|---|---|---|
| N1 | params/result codec host 测试：短包/越界/版本/page 边界/fail-closed | `build/test-results` |
| N2 | AVD/真机：`requeue=1`/`wait=0`/无 panic | `device-gates/` |
| N3 | `page_bytes_written==page.size`、requeue/wait、no panic；`page_verified=false`（内核页不可用 process_vm_readv 读回） | `device-gates/` |
| N4 | child spawn/exec/finish/timeout/kill；uid 原样回报 | `device-gates/` |
| N5 | `victim uid=0`；冷机、固定 CPU、单 route | `device-gates/` |

- N2 复现 K0b 基线；N2/N3/N4 失败即停批；N5 前不标 supported。
- **N2 状态（2026-10-20）**：AVD PASS，`actorsStarted=true`、`requeue=1`、`wait=0`、`pageStage=0`；记录 `docs/analysis/device-gates/n2-actor-20261020-1020-pass.md`。
- **N3 待解**：页 bytes 位于 `WordsRegion` 同一用户映射的 payload 区，内核通过 `alias_delta + page` 访问；需要先取得 alias_delta（legacy leak 或受控 pagemap），actor batch 本身不产生页落点。
- **N3 探针（2026-10-20）**：官方 AVD 上 app 读 `/proc/self/pagemap` 得到 `entry=0`/`pfn=0`；`init_task` 可见，`page_offset_base` 缺失。pagemap 路径不可用，alias 需 legacy leak 或 root 侧辅助。

## 11. 构建/回滚

- CMake option `GL_NATIVE_43499_ACTOR` 默认 OFF；target 不默认进 APK；`nm` 检查只导出声明的 JNI。
- `NativeAttemptPort` 未注入 fail-closed；旧 `KernelPageWriter`/route 保留 `ENOSYS`。
- 回滚：关 CMake option + 不注入 port；不改 Kotlin 判定；legacy 文件不动。
- 风险：PI 时序、posix_spawn/fd 泄漏、join 卡住、page UAF、参数伪造、日志泄密；对应 host/AVD/真机测试。

## 12. Kernelsnitch discover 迁移（N3 前置）

目标：把 legacy `steps::run_discovery` 暴露为可复用的 native discover 端口；Kotlin 传完整搜索参数并解释结果，C++ 只执行 `run_discovery` 一次。

```text
discover(params: ByteArray): ByteArray
```

- Params：`SnitchInputs`/`SnitchSetup`/`RegionInputs`/`SnitchPatience`/`ScanStrides`/`ScanRange`/`counter_offset`/`sweep_tags`，全部由 Kotlin 生成；C++ 不读 profile。
- Result：`verdict`、`found`、`mm_struct`、`probes`、`rounds`、`pokes_wanted`、`error`；Kotlin 解释成功/失败。
- C++ 白名单只允许 `steps::run_discovery` 及其 leak 依赖；禁止 route/judge/retry/profile。
- N1 风格 codec + host fake；AVD 上跑一次 discover，记录 verdict/found；不把 `found` 直接当 alias，alias 仍由页/映射路径决定。
- 该端口命名为 `NativeDiscoverPort`，43499 仅提供参数；其它 CVE 可复用同一端口。
- **首步已实现（2026-10-20）**：`DiscoveryRequest` 增加 `words_region/words_bytes`；`DiscoveryOutcome` 增加 `words_base/words_bytes`；`run_discovery` 在调用方提供 mapping 时不再 map/unmap，内部 mapping 在成功/失败路径统一释放。
- **Discover 端口已通（refusal）**：`NativeDiscover43499Port` + JNI `nativeDiscover` + 120/64 codec；AVD 小 words 映射返回 `ran=false/refusal=5`，未跑搜索。
- **Kotlin 处理层**：`DiscoverProcessor` 把 discover 原始读数换成候选 `alias/pageBase/fakeTask/fakeLock/waiterWord`，非 canonical/溢出/未运行显式拒绝；C++ 不做 alias 计算。
- **AVD discover 实跑（2026-10-20）**：直接映射扫描 `ran=true`、`verdict=1`、`found=0xffffff80002d7800`（mm_struct）、`probes=2911`、`pokes=65536`、`wordsBase/wordsBytes` 回传；`DiscoverProcessor` 产出候选 `pageBase/fake slots`。
- **N3 staging（2026-10-20）**：`run_attempt` 先 `process_vm_writev` 写 16 KiB 页，再 actor：`pageBytesWritten=16384`、`requeue=1`、`wait=0`。
- **N3 candidate（2026-10-20）**：discover 命中 `0xffffff80000da400` → `DiscoverProcessor` 候选 → 页写 16 KiB → actor `requeue=1`/`wait=0`。仍无内核页读回/root 证明，N3 未通过。
- **N3 page-send（2026-10-20）**：`NativeAttemptParams.pageFd` + C++ `write(fd,page,len)`；AVD 管道回读 16384 字节一致、actor `requeue=1`。短写/bad fd 已改为不启动 actor（`pageErrno=9`、`actorsStarted=false`）。production shaping fd 由 prepare 提供后替换管道。
- **Prepare 端口（2026-10-20）**：`NativePrepare43499Port`/JNI/192/160 codec + `WordsRegion` handle；AVD 成功 `status=0`、`base=-549752602624`、`shapingFd=82`、`snitchFound` 非零；按 `payload_delta=-0xE80` 建 64 KiB skb 页、经 prepare fd 送出，`pageBytesWritten=65536`、actor `requeue=1`。仍无内核页读回/uid=0，N3 未通过。

## 13. 批准记录

- 原文（保留供沿革）：用户 2026-10-20 允许 native 多线程与 child；Kotlin 保持完备参数/判定；C++ 以最小逻辑为原则，必要部分可放 C++。
- **适用范围（2026-10-21 口径）**：该记录只构成**原则允许**；**生产启用**仍是待批准（kt-step §0 只批第 1 层）。
  逐项状态见 [43499-approval-status.md](43499-approval-status.md) §3——本文件不得再单独声明「已批准」。
- 本计划是“C++ 仅 syscall”的明确偏离；N1 前需 design-critic 关闭阻断。
