# 43499 legacy C++ 拆解与 JNI 实施指南

- 日期：2026-10-23；级别：L；状态：已批准 v4 的实施细化，非新架构权威。
- 上位权威：[JNI 重写计划 v4](43499-43284-jni-rewrite-plan.md)、[ADR-0021](../analysis/adr/0021-jni-abi-and-ownership.md)、[边界摘要](../analysis/kotlin-native-boundary-brief.md)、[UML](../development/full-process-uml.md)。冲突时以上位权威为准。
- 文件级处置：[生产文件 inventory](43499-legacy-cpp-production-inventory.md)；测试承接：[测试 inventory](43499-legacy-cpp-test-inventory.md)。
- 当前事实：根 CMake 未编译或 include `glk43499/legacy/**`。旧树只作逐 symbol 考古来源；新 C11 禁止 include 或整段复制 legacy。
- 本指南不定义具体地址、payload 值或机制参数，不声明设备支持。

## 1. 最终依赖与文件闭包

```text
Kotlin/Lua
  |-- NativeCommonJni -> libghostlock_common.so
  |     kernel_discovery.c  kernelsnitch/futex_hash.c
  |     handle_registry.c   jni_kernel_discovery.c
  |
  `-- Native43499Jni -> libghostlock43499.so
        page_resource.c pi_actor.c
        multicast_mechanism.c select_mechanism.c tcp_zerocopy_mechanism.c
        victim_protocol.c finish_script.c
        handle_registry.c jni_43499.c

gl_general STATIC -> common/43499/43284
```

每个 DSO 的 registry 私有且 ID 域隔离，绝不跨 DSO 共享。backend 不链接、不调用 common，也不接收 common handle。common 的 copied facts 先回 Kotlin，再由 Kotlin 计算并发给任意 backend。general 只接纳至少两个 DSO 已真实复用、无 backend 类型/默认值/业务可写状态的实现。

Kotlin/Lua 唯一拥有 profile、地址、payload、mechanism 选择、SELinux overwrite / forge credential / seccomp bypass、重试、fallback、跨 backend 顺序、结果解释与 Finish 策略。Native 只拥有 syscall、线程、fd/mmap/socket、跨调用物理资源和不可拆 actor transaction。Access 仅是 stable backend handle 的 borrowed read/write view，不包装全部 JNI。Lua 可在一次 RunScope 内跳过预制 Flow，自由组合 public wrapper 与 Access。

## 2. 文件处置规则

| 标签 | 处理 |
|---|---|
| R 改造复用 | 保留资源/时序语义，以 C11 在新 owner 文件重写 |
| S 拆分 | Native 机制写入新 C11 owner；值计算/选择/编排另写 Kotlin |
| G 下沉 | 两个以上 DSO 真实使用后才进入 general，否则留唯一消费者 |
| K 迁移 | Kotlin 成为唯一权威；Native 仅做 ABI、边界和资源状态校验 |
| X 退役 | 新承接测试与门禁通过、caller/symbol 清零后从源码树删除 |

源码不搬入 `docs/archive/`。批准批次从源码树删除，Git 历史保留；`docs/archive/` 只保存退役记录和索引。删除门必须同时证明 source path、include/call reference、generated manifest、APK/DSO symbol 四类为 0。

## 3. 核心拆分

### 3.1 common discovery

`kernelsnitch/kernelsnitch.h` 拆成 `kernel_discovery.c`；`futex_hash.h` 迁 common 私有实现。移除 target/profile/global/direct-map、文本参数和默认策略。bounds、batch、线程数、timeout 由 Kotlin 输入；common 只复核 0/1/MAX、乘加溢出与资源上限。worker 改 joinable；partial-start/cancel/close 全部执行 stop→join→munmap。continuation cursor 只在 handle 内；facts 是复制值，close 后仍有效。

`number_parse.h` 退役。clock/affinity/rlimit 等 helper 先留 common；只有另一个 DSO 真正调用同一实现后，才把纯基础部分下沉 general。hash/scan 语义始终留 common。

### 3.2 Page

`heap_context.*` 的 page/mapping/fd/reclaim 物理所有权迁 `page_resource.c`。地址选择、用途、payload bytes、metadata 构造、重试全部归 Kotlin。`address_space.*`、`payload_builder.*` 退役；Native 只检查 final address 的非零/边界/对齐/溢出，以及 ABI 表明列的 payload 长度/tag/byte-offset invariant。

Page 是 registry resource；Kotlin wrapper 是逻辑 owner。Actor prepare 原子增加 persistent borrow；失败原子回滚。`destroyPage` 在 borrow>0 时返回 `BUSY`。dirty 时 Actor、Page 和关联 fd 的完整依赖闭包共同 quarantine，禁止单独释放 Page。

### 3.3 Actor 与 mechanism

`pi_race.*`、`threads.*` 拆到 `pi_actor.c`，只保留 actor 资源、线程启动、协作停止、disarm、join 与 effect 发布。完整攻击、重试或三条 route 都不是 Native 原子；不可拆边界仅为：

```text
start actor -> establish physical state -> execute one named mechanism
-> publish result -> stop/disarm -> join -> settle borrow/effect
```

三个旧 route 单元分别迁成三个具名 mechanism C 文件。Kotlin 根据自己的值选择调用哪个 entry；Native 不接收或推断 `RouteKind`。所有差异在 PI 窗口前固定；窗口内无 JNI、registry lock、分配、日志、首次触页、switch 或函数指针分派。三个 JNI entry 各 direct call 对应 C symbol。

catalog、policy、controller、middleware、pipeline、orchestrator 全迁 Kotlin/Lua并退役。旧 lifecycle 模板不保留为回调框架，其物理顺序显式写入 C11 actor/mechanism 并由测试承接。

### 3.4 session、victim 与 Finish

`ExploitSession` 不重建为巨型 Native session；其资源拆成 Page/Actor/Victim typed handle，配置、stage、run state 迁 Kotlin。43499 backend 的 SELinux overwrite / forge credential / seccomp bypass、verify、retry、fallback、handoff 编排全部迁 Kotlin/Lua。64560 占位符退役，不建空 backend，也不假定其依赖 PI actor。

普通 victim 使用 Kotlin `ProcessBuilder`。只有必须同地址空间的协议子进程进入 `victim_protocol.c`，Native 拥有 pid/pgid/pipe；终结顺序为 close writer→grace→kill process group→waitpid→close pipe。

`handoffVictim` 是必须存在的原子 owner transfer：成功时 consume `VictimHandle`、创建 `RootedVictimHandle`，旧 ID 立即 stale；失败在 transfer 线性化点之前由旧 Victim owner 清理，之后由 RootedVictim owner 清理。`handoff × cancel/close` 必须固定唯一赢家且恰一次终结。

UI 生成包含 root 管理器启动、SELinux 恢复等内容的 handoff/Finish 脚本。Native `finish_script.c` 只负责按 v4 规定打开并对同一 fd 做属性/hash 校验后触发；不生成、选择或解释脚本。marker/module/log/SELinux 检查和 finally 删除由 Kotlin Finish 完成。

### 3.5 support/profile

`native_resource.*` 按 owner 拆入 registry/page/actor/victim；fd/mmap/thread/child 的纯基础 cleanup 只有达到复用门槛才进 general。`native_result`、clock 同理。禁止新建万能 `util.c`、`decls.h` 或聚合头。

profile model/accessor/binary/entry/macros 全部迁 Kotlin；`LegacyProfileConverter.kt` 仍是唯一迁移点。Native 不读 target config、不带 fallback offsets、不解析 profile。ABI POD 只能由单一定义生成。

## 4. JNI 定义

### 4.1 Kotlin 层

```text
core/nativeabi/       status/effect/result DTO
core/nativehandle/    OwnedNativeHandle/RunResourceScope
data/nativecommon/    internal NativeCommonJni + Discovery wrapper
data/native43499/     internal Native43499Jni + Page/Actor/Victim wrappers
core/access/          borrowed read/write capability
core/flow/ + Lua      policy and composition
```

`external` 只放 package-private Java bridge 或 private/internal Kotlin holder。public wrapper 不暴露 `Long`，构造器不公开；Lua 只得到 scope-bound facade。资源创建返回 result DTO + opaque ID，值查询返回 copied DTO/primitive array。禁止 raw pointer、通用 opcode、`Map<String, Any?>`、大 frame、C++ class/string/span/expected 跨 JNI。

ABI header、status/effect、kind 与 request/result header **沿用 v4 §4 的冻结定义**，并由唯一 ABI 定义生成 Kotlin declaration、C prototype、validator 与 JNI manifest；本指南不复制第二份字段权威。实现优先 `RegisterNatives`。`JNI_OnLoad` 只注册函数并初始化本 DSO registry，不读取业务配置。

### 4.2 43499 typed entries

| Kotlin entry | C entry | 语义 |
|---|---|---|
| `allocatePage` | `gl43499_allocate_page` | 创建 Page owner |
| `mapPage` | `gl43499_map_page` | 映射物理资源，不选业务地址 |
| `installPayload` | `gl43499_install_payload` | 接收 Kotlin final bytes |
| `prepareActor` | `gl43499_prepare_actor` | 创建 Actor 并建立 Page borrow |
| `executePselectActor` | `gl43499_execute_pselect_actor` | pselect 物理入口；不接收 route |
| `executeSendmsgActor` | `gl43499_execute_sendmsg_actor` | sendmsg/mcast 物理入口；不接收 route |
| `executeTcpZerocopyActor` | `gl43499_execute_tcp_zerocopy_actor` | TCP zerocopy 物理入口；不接收 route |
| `cancelPreparedActor` | `gl43499_cancel_actor` | 只置 stop，不释放 |
| `destroyActor` | `gl43499_destroy_actor` | clean 才解除 borrow并释放 |
| `destroyPage` | `gl43499_destroy_page` | borrow>0 返回 BUSY |
| `createProtocolVictim` | `gl43499_create_protocol_victim` | 创建协议子进程 owner |
| `handoffVictim` | `gl43499_handoff_victim` | consume Victim，产生 RootedVictim |
| `destroyVictim` | `gl43499_destroy_victim` | 终止并 wait；接受明确 kind |
| `executeFinishScript` | `gl43499_execute_finish_script` | 消费 RootedVictim 的受控操作，不生成脚本 |

禁止增加 `executeRoute`、`prepareAttack`、`run43499` 或 `invoke(opcode, bytes)`。

### 4.3 common entries

```text
openDiscovery -> DiscoveryHandle
scanDiscovery(handle, batchRequest) -> ScanReceipt
readFacts(handle, receipt) -> copied DiscoveryFacts
cancelDiscovery(handle) -> NativeCallResult
closeDiscovery(handle) -> NativeCallResult
```

Discovery ID 只在 common DSO 有效。close 后 scan/read 为 `STALE_HANDLE`；copied facts 继续有效。

## 5. Registry、终结与并发

每个 owned entry 传 `scopeId/cookie`；registry 对拍 slot owner、kind、generation、epoch、state，再增加 `inFlight`。锁外执行，返回前 unpin。close 线性化为 OPEN→CLOSING，等待 inFlight=0；dirty/unknown 进入 QUARANTINED，不做普通 destroy。

### 5.1 unload

应用不得主动 unload DSO。`JNI_OnUnload` 只有在 slot 为 CLEAN/DESTROYABLE 且 `inFlight==0 && persistentBorrow==0` 时可 clean-only 回收；QUARANTINED、LEAKED_ON_STUCK、dirty、unknown、inFlight 或 borrowed slot 必须记录并钉住到 OS 进程退出，禁止调用普通析构。D1 白盒覆盖 empty、clean、dirty、unknown、inFlight、borrowed 与容量边界。

### 5.2 fork

注册 `pthread_atfork`：prepare 锁本 DSO registry；parent 解锁；child 只做目标 Android 已验证的 async-signal-safe mutex reset、epoch 增长和 slot 清空。所有非协议 fd 创建即 `CLOEXEC`；child exec 前只用预生成 fd allowlist 与 async-signal-safe `close` 关其余 fd，不释放 mmap/复杂对象。pre-fork ID 在 child 因 epoch 不符全部 `STALE_HANDLE`。D3 真机覆盖 fork-while-execute、fork-while-close 和 `/proc/self/fd` allowlist。

### 5.3 并发判据

| 竞争 | 固定结果 |
|---|---|
| execute×execute | 后到者 `INVALID_STATE` |
| execute×cancel | 协作终止并保留准确 effect |
| execute×close | close 等待 inFlight=0 |
| close×close | 只释放一次 |
| prepareActor×destroyPage | prepare 建 borrow或完整回滚；destroy 对 borrow 返回 BUSY |
| handoff×cancel/close | transfer 线性化点决定唯一 owner；旧 ID stale；恰一次终结 |
| dirty actor×destroyPage | 完整依赖闭包 quarantine |
| fork child 使用旧 ID | `STALE_HANDLE` |

JNI 不用异常表达进入状态机后的失败，必须返回 status+errno+effect。只有尚未进入资源状态机的 JVM 参数对象错误可抛 `IllegalArgumentException`。

## 6. 实施批次与删除门

1. **A0 inventory**：填满两个 inventory 和逐 symbol annex；冻结 caller、owner/borrow/终结、old assertion→new test。
2. **A1 ABI/registry/JNI shell**：生成 ABI、两个 DSO 私有 registry、两个 JNI bridge；跑 JVM/host 极端状态。
3. **A2 common**：迁 discovery；backend link-map 的 common symbol=0；common 真机单功能。
4. **A3 page**：迁 page owner；验证 partial failure、borrow、double-close、quarantine。
5. **A4 actor/mechanism**：一次一个 mechanism；每批 direct-call 反汇编与单机制冷机真机门禁。
6. **A5 Kotlin**：接 address/payload/Flow/Lua；证明 mechanism 选择只在 Kotlin且 Lua 可绕过预制 Flow。
7. **A6 victim/Finish**：验证 transfer、pid/pgid、wait/kill、same-fd hash、marker/log/SELinux/删除。
8. **A7 retirement**：inventory 零缺项；四类清零；归档退役记录。

逐 symbol annex 必填：legacy source:symbol、处置、target、输入校验、输出/errno/effect、created resource、owner、borrow interval、stop/disarm、终结者、各退出路径、行为差异、old test→new test、host/NDK/JNI/device 证据。任一项空缺不得迁移或删除。

攻击关键路径批同批真机；连续失败两次回 debug。PI 内存与 waiter 保持“停止访问→同步/解除关联→回收”。未验证路径不得标 supported。

## 7. 完成判据

- 新 source/include 对 legacy 为 0；backend link-map 对 common symbol 为 0。
- Native entry 无 `RouteKind`；Kotlin 是 address/payload/profile/SELinux overwrite → forge credential → seccomp bypass/retry/fallback/Finish 唯一权威。
- old main、`run43499/create43499/destroy43499`、pipeline/catalog/controller 不出现在生产 APK/DSO。
- ABI/JNI manifest 单一生成；registry 的 unload/fork/borrow/quarantine/transfer/concurrency 矩阵通过。
- 每个旧 production symbol 和 test assertion 有可核承接；host、Kotlin、NDK、nm/readelf/link-map 与独立真机证据完整。
