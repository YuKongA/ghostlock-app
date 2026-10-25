# Kotlin / JNI / Native 分层摘要（v3 已批准）

- 日期：2026-10-23；级别：L；状态：已批准（用户指令“按照计划”）。
- 完整权威计划：[JNI 重写计划 v4](../plan/43499-43284-jni-rewrite-plan.md)。本文只保留结构摘要，避免第二权威。
- 生效：本文、plan、ADR-0019/0021/0022、approval-status、UML 经同一冻结快照评审且用户一次批准后同步生效。

## 1. 分层

```text
Lua -> FlowLuaImpl.run -> scope-bound Kotlin API
UI/default Flow/custom Kotlin
  ├─> general JNI -> copied DTO -> Kotlin -> backend
  ├─> 43499 JNI
  └─> 43284 JNI
common STATIC -> general/43499/43284
```

- **Kotlin/Lua**：profile、地址、payload、route、SELinux overwrite / forge credential / seccomp bypass、重试、fallback、跨 backend 顺序、结果解释、Finish。
- **backend JNI**：漏洞机制、物理资源、不可拆 transaction；不接收 `RouteKind`。
- **general JNI**：仅 Kotlin 调用的 discovery/scan；backend 不调用/link general，也不接收 general handle。
- **common STATIC**：至少两个 DSO 真实复用的无业务状态基础实现。
- **UI Finish**：生成 root manager/载荷/SELinux 恢复脚本；Native 不生成或选择脚本。

## 2. 关键归属

| 能力 | 归属 |
|---|---|
| KernelSnitch、futex hash/collision scan | general JNI |
| 薄 futex syscall helper | general；43499 也复用后才下沉 common |
| waiter/owner/consumer、PI actor | 43499 |
| 地址、payload、route | Kotlin/Lua |
| SA/sequence/page-cache session | 43284 |
| 64560 | 当前无实现；不建空 backend，不依赖 43499 actor |

## 3. 接口

- Kotlin `object NativeGeneralJni/Native43499Jni/Native43284Jni` 承载 external entry。
- 有 owner 的跨调用资源由 `AutoCloseable class` 包装 typed opaque handle；地址、payload、candidate、errno、receipt 是 DTO。
- Access 只是 stable backend handle 的 borrowed read/write view；不 prepare、scan、route、handoff，也不包装全部 JNI。
- Lua 保留任意 public Java/Kotlin 反射能力；受支持 wrapper 必须绑定本次 `RunScopeToken`，无 scope 创建 owner 时 fail-closed，run finally 逆序 close。
- UI run 仍经 Flow；Lua 脚本内部可以跳过 backend 预制 Flow，自由交错调用 backend wrapper/Access。

## 4. 43499 原子边界

Native 不把完整攻击称作原子；只有 PI actor transaction 不可拆：

```text
启动三 actor -> 建 PI -> 执行一个 mechanism -> 发布结果
-> stop/disarm -> join -> 清 borrowed ref -> 返回 effect
```

唯一 **route-agnostic** entry：`executeActor`；不按 route/mechanism 命名，也不接收 route。机制差异在 Kotlin 传入的参数里；JNI 入口在 PI 窗口前完成准备，窗口内无 switch 间接目标、函数指针、JNI、registry 锁、分配、日志或首次触页。

## 5. owner 与 dirty

- 每 DSO 一个固定容量 C11 registry；ID 含 DSO/slot/generation/epoch 校验，不含 pointer。
- entry 以 inFlight pin 防 close UAF；Actor 对 Page 用 persistent borrow，Page close 在 borrow>0 返回 BUSY。
- fork child 重建 registry mutex、递增 epoch、清 slots；旧 ID 全 stale。应用不主动 unload DSO。
- clean：stop→join→解除 borrow→destroy。
- dirty：Actor 的 Page borrow 不解除，Actor/Page/关联 fd 的完整依赖闭包转 `QUARANTINED/LEAKED_ON_STUCK`；当前 run fail-stop，Cleaner 禁止释放，资源保留到进程退出。容量满返回 CAPACITY。
- 详细状态、并发矩阵、CBN 见 plan §5–§6。

## 6. victim / Finish

- 普通 victim 创建和验证继续使用 Kotlin `ProcessBuilder`。
- 必须与 43499 transaction 同地址空间的 protocol child 才由 Native `VictimHandle` 持有 pid/process-group/pipes。
- rooted transfer 后旧 handle 失效；失败按 close-writer→grace→kill process-group→waitpid→close pipe 清理。
- `executeFinishScript(rootedVictim, path, expectedHash)` 是唯一触发原语；Native 打开后对同一 fd 做 fstat/hash，并以 `/proc/self/fd/<fd>` 在 rooted child 中执行，避免验证与执行对象分离。
- Kotlin prepare/patch/await marker-log-SELinux/finally cleanup；完整 sequence 与真机判据见 plan §7/§9。

## 7. legacy 处置

| legacy | 目标 |
|---|---|
| race/pi_race、threads | 43499 pi_actor |
| 三 route 实现 | 三 mechanism；route 组合移 Kotlin |
| heap_context | 43499 page_resource |
| kernelsnitch | general JNI |
| payload/address/profile/offset | Kotlin 唯一权威 |
| route policy/controller/session state | Kotlin Flow/run state |
| victim | 普通 ProcessBuilder；protocol child 留 handle |
| root_child/handoff_probe | Kotlin Finish + 中性触发 primitive |
| 64560 stub/main/common.h | 不进 manifest，逐 symbol 映射后退役 |

生产实现为 C11；legacy C++ 只读参考，不 include、不复制、不进 source list。D3 前逐 symbol 填 source→target、输入/输出/effect、owner/borrow/终结、差异和验证。

## 8. 取代与批准

该候选请求知情取代：

1. per-route JNI/TU → per-mechanism 命名 entry，route 在 Kotlin；
2. 全无状态/无 handle → 值调用无状态，物理资源 typed handle；
3. 可写全局为空 → 每 DSO registry 是唯一例外；
4. Flow 独占能力入口 → UI run 经 Flow，但 Lua 脚本可直调 scope-bound API；
5. actor batch 生产、protocol child/Finish primitive 进入后续真机批次。

代价：动态组合误用面和 registry 状态增加。偿还：typed schema、并发反例、link/include firewall、三 route 分别真机。未获用户明确批准前，旧权威保持有效。

## 9. 通过标准

- plan/ADR/approval/UML 候选口径一致；design-critic 阻断 0；用户明确批准。
- route 仅在 Kotlin/Lua；KernelSnitch 仅 general；PI actor 仅 43499。
- 每批记录当前 Gradle/CMake exact command、XML 三桶、NDK、nm/readelf/link-map、APK/DSO hash。
- 攻击批当批完成冷机、固定 CPU、单 route、KSU 未加载真机门禁；未过不得标 supported。

**共识：common 是 Native 静态基础库，general 是 Kotlin 独占的 discovery 服务，backend 是可组合的漏洞机制与资源；Kotlin/Lua 用地址、payload、handle 和调用顺序组成 route。**
