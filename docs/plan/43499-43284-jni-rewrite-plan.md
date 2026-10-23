# Kotlin-first JNI 重写计划 v4（批准候选）

- 日期：2026-10-23；级别：L；作者：Lead；评审：design-critic。
- 状态：**候选**。本文、ADR-0019/0021/0022、approval-status 和 UML 由用户一次批准后同步生效；此前 v3 仍是旧口径。
- 取代：v4 生效时取代 v3 的 per-route JNI、全无状态/无 handle 和 Flow 独占能力入口。
- 摘要：[Kotlin/Native 边界](../analysis/kotlin-native-boundary-brief.md)。

## 1. 决策与依赖

```text
Lua -> FlowLuaImpl.run -> scope-bound Kotlin APIs
UI/default Flow/custom Kotlin
  ├─> common JNI -> copied DTO -> Kotlin -> any backend
  ├─> 43499 JNI
  └─> 43284 JNI
general STATIC -> common/43499/43284
```

- Kotlin/Lua 唯一拥有 profile、地址、payload、route、W1/W2/W3、重试、fallback、跨 backend 顺序和 Finish 策略。
- backend JNI 只拥有漏洞机制、跨调用物理资源和不可拆 transaction；不接收 `RouteKind`。
- common JNI 只供 Kotlin 调用；backend 不调用/link common，也不接收 common handle。
- general 是 Native 静态基础库；准入要求至少两个 DSO 已真实复用、无 backend 类型/默认/可写业务状态。
- C11；legacy/very_legacy 只读参考，不进入 source/include list。

## 2. 同批取代

| 旧权威 | v4 决策 | 生效登记 |
|---|---|---|
| v3 §4.1、approval §4：per-route JNI/TU | 每个物理 mechanism 命名 entry；route 在 Kotlin | approval route 表 |
| v3 §4.2、ADR-0021：无 handle | 值调用无状态；资源用 typed handle | approval handle 表 |
| ADR-0019：可写全局为空 | 每 DSO 一个固定容量 registry 例外 | ADR-0019/C5 |
| ADR-0022：Flow.run 唯一入口 | UI run 仍经 Flow；Lua 脚本内部可直调 scope-bound API | ADR-0022 |

代价：route TU 静态隔离减少；增加 request 误组合和 registry 状态。偿还：typed request、状态/并发反例、link firewall、三 route 单独真机门禁。该取代需用户知情确认。

## 3. DSO 与目录

```text
general/                 gl_general STATIC
  gl_log.c native_clock.c native_result.c native_cleanup.c
  futex_syscall.c        # 仅当 common+43499 均实际调用
glkcommon/               libghostlock_common.so; Kotlin-only
  kernelsnitch/futex_hash.c kernel_discovery.c jni_kernel_discovery.c
glk43499/                libghostlock43499.so
  page_resource.c pi_actor.c select_mechanism.c
  tcp_zerocopy_mechanism.c multicast_mechanism.c jni_43499.c
glk43284/                libghostlock43284.so
```

AES 当前仅 43284 使用，留 43284。futex helper 若只有 common 使用也留 common；达到两个真实消费者才下沉 general。64560 不建空目录；旧 stub 不进 manifest。

## 4. ABI 与权威

每个 entry 使用具名 typed request/result；禁止通用 opcode/map/frame。单一定义生成 Kotlin 字段、C validator、handle kind 与 JNI manifest；生成器未落地前用 `_Static_assert`、Kotlin size/offset test 和逐字段对拍。

```text
RequestHeader { version:u16, size:u16, requestId:u64 }
ResultHeader  { version:u16, size:u16, status:u32, errnoValue:i32, effect:u32 }
```

| 数据 | 权威 | Native 仅可检查 |
|---|---|---|
| finalAddress/domain bounds/tag | Kotlin | 非零、上下界、对齐、溢出 |
| finalPayloadBytes/metadata | Kotlin | 长度、ABI tag、列入 ABI 表的 byte-offset invariant |
| scan params/bounds | Kotlin | 0/1/MAX、乘加溢出、资源上限 |
| handle state/effect | Native | registry 状态机 |

Native 禁止用 route/offset/profile 重算地址或 payload。未列入 ABI 表的 payload invariant 不得检查。requestId 只作观测，不提供重放幂等。

状态：`OK/BAD_ARGUMENT/STALE_HANDLE/WRONG_KIND/INVALID_STATE/BUSY/CAPACITY/UNSUPPORTED/CANCELLED_CLEAN/CLEAN_FAILURE/DIRTY_FAILURE/INDETERMINATE`；errno 独立返回。

## 5. handle registry

### 5.1 选择与 CBN

| 方案 | 收益 | 成本/边际 |
|---|---|---|
| A 每 DSO 固定 registry（选） | raw pointer 0 次跨 JNI；wrong-kind/stale 可统一拒绝 | 1 个锁+固定 slots；每新 handle kind 改定义/生成物各 1 处 |
| B caller-owned arena pointer | 无 registry 全局 | 必须安全映射 pointer token；每 entry 增加 arena 参数，仍需生命周期表 |

翻转条件：registry 使 PI 窗口新增锁/间接分派，或固定容量不足以覆盖“一次 run 最大资源数×2”时重评。所有 registry 操作在 PI 窗口外。

### 5.2 私有实现

slot 内部布局不属于跨端 ABI；至少含 kind/state/generation/epoch/inFlight/persistentBorrow/resource。ID 编码 DSO tag、slot、generation、epoch 校验片段，不含 pointer。generation 回绕永久退役 slot。

- `JNI_OnLoad` 初始化；应用进程不主动 unload DSO。
- `JNI_OnUnload` 绝不普通 destroy `QUARANTINED/LEAKED_ON_STUCK`、inFlight、borrowed 或状态未知的 slot；只记录并钉住，最终由 OS 进程退出回收。只有状态 CLEAN/DESTROYABLE 且已证明 `inFlight==0 && borrow==0` 的 slot 才可 clean-only 回收；应用仍不得主动 unload DSO。D1 覆盖 OnUnload×dirty 静态分支和状态测试。
- 注册 `pthread_atfork`：prepare 锁 registry，parent 解锁；child 仅做已在目标 Android 验证的异步安全 mutex reset、递增 epoch、清 slots。所有非协议 fd 创建时设 CLOEXEC；child exec 前用预生成的 fd allowlist 和 async-signal-safe `close` 关闭其余 fd，不在 child 释放 mmap/复杂对象。pre-fork ID 因 epoch 不匹配全部 stale；D3 对 `/proc/self/fd` allowlist 和 fork-while-execute/close 做真机检查。
- entry 在锁内校验并 `inFlight++`，执行后 `--`；close 线性化为 OPEN→CLOSING，等待 inFlight=0 后移除 slot并释放。
- cancel 只置 owner 的原子 stop，不释放资源。
- Actor 对 Page 的 borrow 在 prepare 时原子 `persistentBorrow++`。只有 CLEAN/CANCELLED_CLEAN 在 stop/join 后恰一次 `--`；DIRTY/LEAKED_ON_STUCK 不解除 borrow，而是原子把 Actor、Page 和关联 fd 的完整依赖闭包全部转 QUARANTINED。Page close 在 borrow>0 返回 BUSY。

并发判据：execute×execute=INVALID_STATE；execute×cancel=协作终止；execute×close=close等待；close×close=一次释放；prepareActor×destroyPage=BUSY；fork-while-execute/close 的 child ID=STALE。

## 6. 资源与状态机

### 6.1 common discovery

`openDiscovery/scanDiscovery/readFacts/cancelDiscovery/closeDiscovery`。KernelSnitch 移除 target/profile/global/direct-map 依赖；所有 bounds、线程数、batch、timeout 由 Kotlin 输入。detached worker 改 joinable；partial-start/cancel/close 均 stop→join→munmap。

continuation 只存在于 handle 内部 cursor；Kotlin 只传 batch index/receipt。close 后继续 scan=STALE。DTO 是复制值，common close 后仍有效。

### 6.2 43499

```text
allocatePage/mapPage/installPayload
prepareActor
executeMulticastActor / executeSelectActor / executeTcpActor
cancelPreparedActor / destroyActor / destroyPage
```

每 mechanism 使用独立命名 entry；分派发生在 JNI 入口、PI 窗口前。三个 entry 各调用具名 C11 direct symbol（`pi_actor_execute_multicast/select/tcp`）；公共代码只允许宏生成或编译期 inline 展开，不传函数指针。所有差异在窗口前准备，PI 窗口内无 switch、BLR/间接 call、JNI、锁、分配、日志、首次触页及禁止 syscall。D3 用反汇编逐 entry 证明直接调用并登记窗口锚点。

```text
PREPARED -> EXECUTING -> COMPLETED_CLEAN -> DESTROYABLE
                     \-> COMPLETED_DIRTY -> QUARANTINED
PREPARED -> CANCELLED_CLEAN -> DESTROYABLE
```

disarm 属于 execute 内部；不导出外部 disarm。`QUARANTINED/LEAKED_ON_STUCK` 采用进程生命周期保留策略，Cleaner 禁止回收；registry 记录资源清单和 effect，当前 run fail-stop。容量耗尽返回 CAPACITY，不再攻击。进程退出是最终终结点；D3 真机后才可设计具名 reap，未经证据不得释放。旧 T-LEAK 的 `pipefd[2]`/`block_fd` 归 Actor owner。

### 6.3 43284 与 Access

43284 的 SA/sequence/session 独立；不抽象成 43499 handle。Access 只是 stable backend handle 的 borrowed read/write view，不拥有、不 prepare、不 scan、不 route、不 handoff。Flow/Lua 可直接调用 backend wrapper。

### 6.4 Lua run scope

`FlowLuaImpl.run` 仍是 Lua UI run 入口。它创建不可伪造的 `RunScopeToken`，作为 coroutine-context element 显式传给每个 owned factory；无 token fail-closed。所有 JNI external 声明放在 package-private Java bridge/private Kotlin holder，Lua 不获得 holder 实例，受支持 API 不暴露 raw `jlong`。Native owned-entry 还校验 scope cookie/scope ID 与 handle owner 一致，不能只信 factory。wrapper 构造不公开，Lua 只获得 scope-bound facade。dispatcher 切换继承 context；新线程/异步回调无 token不能创建 owner。finally 在 NonCancellable 中逆序 close；run 后 userdata 调用=STALE。测试用 `Class.forName`/反射尝试直接访问 bridge并预期失败或 BAD_ARGUMENT。

反射仍可访问任意 public Java/Kotlin API。inventory 只是“受支持脚本 ABI 清单”，不是安全或完整可达清单；清单外不保证兼容，raw-jlong API 不进入清单。

## 7. victim 与 Finish 闭环

- 普通 victim 创建/验证使用已批准的 Kotlin `ProcessBuilder`；旧 native child JNI 继续不做。
- 必须与 43499 同地址空间保持的 victim protocol child 由 `VictimHandle` 创建并拥有 pid、process group、pipe；Kotlin owner 只持 handle。
- `cancel/destroyVictim` 顺序：关闭 command writer→等待宽限→`kill(-pgid,SIGKILL)`→waitpid→关 pipe。handoff 成功把 rooted child 转为 `RootedVictimHandle`，旧 handle 失效。
- 具名 `executeFinishScript(rootedVictim, scriptPath, expectedHash)` 是唯一触发原语：Native 用 `openat(O_RDONLY|O_NOFOLLOW)` 打开，`fstat` 校验 regular file、owner、mode、device/inode并对同一 fd 求 hash；随后通过 `/proc/self/fd/<fd>` 交给 `/system/bin/sh`，该 fd 在解释器完成打开前不得带 CLOEXEC。验证对象与执行对象始终是同一 fd；失败即关闭 fd。Native 不生成/选择/解释脚本。测试在 hash 后替换 path，仍只能执行原 fd 内容。
- Kotlin `FinishExecution.prepare` 生成脚本、memfd、hash并 patch payload；触发后 `awaitCompletion` 验证 marker/module/log/SELinux，最后 `FinishSession.finish` 停 drainer、删脚本、关输入。
- 失败/取消：若未 transfer，VictimHandle owner 清理；已 transfer 则 RootedVictimHandle wait/kill；FinishSession 始终 finally 清理。D5 真机核验 pid/pgid、wait/kill、script/hash、marker、SELinux 和删除。

## 8. legacy 处置与逐 symbol 门

| legacy | 目标/处置 |
|---|---|
| race/pi_race、threads | 43499 pi_actor；保留时序/回收，移 route/profile/global |
| 三 route 实现 | 三 mechanism；只留物理调用/effect |
| heap_context | page_resource；地址决策移 Kotlin |
| kernelsnitch/* | common；纯 helper 按消费者计数决定 common/general |
| payload_builder/address_space/profile/kernel offsets | Kotlin 唯一权威 |
| route policy/catalog/orchestrator/controller | Kotlin Flow/route composition |
| session backend/exploit_session/run_state | Kotlin run state |
| root_child_frontend/handoff_probe | Kotlin Finish + 上述 Victim/Finish primitive |
| victim_process/context | 按 §7 拆；普通 ProcessBuilder，protocol child 留 handle |
| old 64560 stub/main/common.h | 不进 manifest；逐 symbol 映射后退役 |

D3 前必须在现有 annex 完成每个生产 symbol 的 `source file:function → target file:function → 输入/输出/errno/effect → owner/borrow/终结 → 差异 → 验证`。缺一项不得迁移或删除。禁止 include/复制 legacy。

## 9. 批次与精确门禁

| 批次 | 内容 | 离线/构建 | 真机 |
|---|---|---|---|
| D0 | 同批文档候选 | link/check、字节配额、critic 0阻断 | — |
| D1 | schema/wrapper/registry | `./gradlew :app:testDebugUnitTest`；XML三桶；NDK assemble；nm/readelf/link-map | — |
| D2 | general/common | DTO-after-close；0/1/MAX batch；backend link-map common symbol=0 | discovery 单功能 |
| D3 | page/actor/mechanisms | 并发矩阵、registry残留=0、窗口调用图 | 每 mechanism 冷机单 route |
| D4 | Kotlin address/payload/Flow/Lua | fixed vectors、错域/截断/metadata、Lua scope | 三 route 写回读 |
| D5 | victim/Finish/退役 | symbol manifest、旧入口=0、APK hash | uid=0、script hash/marker/SELinux |

D0 只冻结命令，不把 task-list 查询当构建证据：先运行 `./gradlew :app:tasks --all --console=plain > <evidence>` 并记录 Gradle exit，再对 evidence 单独 `rg` 并记录 rg exit。实现批命令为 `./gradlew :app:testDebugUnitTest --console=plain`、`./gradlew :app:externalNativeBuildDebug --console=plain`、`./gradlew :app:assembleDebug --console=plain`。每批保存完整 literal output、exact exit、XML三桶、NDK零告警、APK/DSO sha。共享构建串行；攻击批当批真机；连续失败两次停回 debug。

C5 从“BSS 空”改为“仅 registry 明确符号”；每 DSO 允许表由 manifest 生成，其余可写符号为 0。registry 不得在 PI 窗口访问。

## 10. 批准项与扩展

用户需一次确认：route-TU 旧裁定被 mechanism entry 取代；actor batch 生产启用；per-DSO registry 全局例外；victim protocol child/Finish primitive。

扩展点：新增 backend 只新增自己的 DSO/schema/wrapper；新增 mechanism 只新增命名 entry、PI 直接调用版本和 Kotlin request；common 只有独立 Kotlin-called service 才可加入。未来债：quarantine 可能耗尽固定 slots，D3 后以实测 dirty 率决定是否设计 reap；偿还前 fail-closed CAPACITY。

批准标准：critic 阻断 0、用户明确批准、冻结 manifest 中七份候选（brief、plan、ADR-0019、ADR-0021、ADR-0022、approval-status、UML）同步生效；未满足时不写实现。
