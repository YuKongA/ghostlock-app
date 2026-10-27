# 43499 G4L-1～G4L-6 纯代码冲刺 · 完成记录

- 日期：2026-10-26（America/Toronto）
- 模式：用户知情强制「B 纯代码冲刺」（批准表 §4.2 / [沿革记录](43499-g4l-sprint-override-20261026.md)）
- 基线：`HEAD=9dd7e34c5849681857dc4274d4ab026ceef53df6`（2.0.10）
- 后续指令：用户“**冲刺到G4L-6**”把同一模式扩展到 G4L-5/G4L-6（见 §7）。
- 结论：**G4L-1、G4L-2、G4L-3、G4L-4M、G4L-4P、G4L-4T、G4L-5、G4L-6 八批代码实施完成**（Native 编译+符号证据、Kotlin 编译证据）；评审/门禁/测试/真机验收全部未做，已登记为补验批技术债（task-9）。
- 未 commit / 未 push；工作树新增内容待用户决定（提交或先补验）。

> **⚠ 2026-10-26 用户终验不通过（10 项结构阻断，见 [43499-g4l-final-review-20261026.md](43499-g4l-final-review-20261026.md)）；随后完成 F-A～F-D 结构修复（见 [43499-g4l-structural-fix-20261026.md](43499-g4l-structural-fix-20261026.md)）。本记录的 §1–§7 是**冲刺时快照**，文件清单已被修复批扩充（新增 FinishExecution/FinishExecutionEvidence 等，actor/jni/placement/page/carrier/FinishScript 有修改）；当前文件状态与验证据以结构修复记录与工作树为准。当前仍非 PASS/supported，真机门禁待设备。**

## 1. 交付物

新增 C11 私有实现（`app/src/main/cpp/gl43499/core/`，共 11 文件、4212 行）：

| 批次 | 文件（行数） | 内容 |
|---|---|---|
| G4L-1 Placement | `placement.c`(867)、`placement_internal.h`(162)、`core_internal.h`(255)、`kernel_internal.h`(162) | mm 塑形 children/memfd、staging、KernelSnitch 定位测量、facts 回填、`stop→close→reap` 逆序清理；cancel 不消费、close 接受后消费 |
| G4L-2 Landing/Page | `page.c`(395)、`page_internal.h`(69) | preflight/commit 冻结；commit 前失败 `placement_consumed=0`、commit 后（含 CLEAN_FAILURE/DIRTY/INDETERMINATE）`=1`；reclaim socketpair 闭包；closePage |
| G4L-3 Actor | `actor.c`(1138)、`carrier_internal.h`(227) | 三 worker（consumer→owner→waiter 创建、join waiter→owner→consumer）、PI 窗口、Page 提交点、四段终结顺序、cancel/partial-start、execute 恰好一次 |
| G4L-4M Multicast | `carrier_multicast.c`(171) | `MCAST_BLOCK_SOURCE` stamp + consumer 握手 + 有界 disarm |
| G4L-4P Pselect | `carrier_pselect.c`(337) | fd_set 窗口、低号 fd 安装、stdio 备份/恢复、stuck 保留 |
| G4L-4T TCP zerocopy | `carrier_tcp.c`(429) | loopback pair、打洞 memfd+mmap、punch worker、`TCP_ZEROCOPY_RECEIVE` |

修改/删除：
- `app/src/main/cpp/CMakeLists.txt`：`ghostlock43499` = `jni43499.c` + `placement.c` + `page.c` + `actor.c` + `carrier_multicast.c` + `carrier_pselect.c` + `carrier_tcp.c`，加 `-D__ARM`。
- `app/src/main/cpp/gl43499/core/api_unavailable.c`：**已删除**（九入口全部真实实现，每符号单一强定义；CMake 同步移除）。
- 零改动：公共 C11 头 7 个、`jni43499.c`、Kotlin/profile、`legacy/legacy2/very_legacy`、`glgeneral`、43284、UI。

## 2. Lead 独立证据（冲刺模式可做的范围）

```
COMMAND=./gradlew :app:externalNativeBuildDebug --console=plain
INPUT=工作树（六批实现全部接入）
LITERAL_OUTPUT=BUILD SUCCESSFUL in 308ms; 4 actionable tasks: 4 executed
EXIT_STATUS=0 RESULT=PASS
```

- DSO：`build/app/intermediates/cmake/debug/obj/arm64-v8a/libghostlock43499.so`，234728 B，sha256 `550baeba19832f1b08842525eb6cddc3edbcc1ba8370335cb80103f873143c57`。
- `nm -D`：9 个 `Java_com_ghostlock_app_jni_Native43499Jni_*` 导出（与 Kotlin 9 个 external 一一对应）。
- 静态符号表：9 个 core 入口（`prepare/cancel/close_placement`、`land_page/close_page`、`prepare/execute/cancel/close_actor`）**各恰好 1 个定义**（hidden `t`），无第二强定义。
- 可写全局：`.data/.bss` OBJECT=0；旧 Flow/session/route/registry 符号=0。
- `git diff --check`=0；11 个新源码文件行尾空白=0；新码 TODO/FIXME/`using`/legacy include=0。
- 静态检查：公共头/JNI/Kotlin/legacy/glgeneral 改动=0。

## 3. 每批语义选择（契约缺口，保守实现；补验批需逐条确认）

1. **content_generation**：Kotlin 提供、Native 原样透传回执，不参与接受/生命周期/防伪判定。
2. **execute 恰好一次**：第二次 execute 返回 `INVALID_ARGUMENT` 且不改物理状态；超时即 dirty retained。
3. **scan_step/scan_round_limit**：step=候选基址额外对齐粒度（pow2 且整除 allocation_size）；round_limit=测量重试上限（≤8，每轮重建塑形）；为冲刺期自定语义，需真机校准。
4. **数量/几何常量**：`page_size==4096`、`allocation_size==0x8000`、`objects_per_slab==25`、`prepare=8×objs`、`spray=6×objs`、`pre=objs-1`、`post=objs`（旧实现编译期常量）；G4L-5 wrapper 落地前须按此传值。
5. **PI 窗口间接调用**：窗口内唯一 BLR 为 prepare 阶段绑定的 `waiter_trigger`（无 kind 读取/无 switch/无分配/日志/锁）；若补验批要求零 BLR，需改为每 kind 独立线程入口（跨 M/P/T 三批）。
6. **TCP**：frame=zc 请求体模板，Native 只写 `address=mapping_base+mapping_offset`（offset 0，依赖内核 ABI 首成员）；punch worker 作为 Carrier 内部第 4 线程，`Actor.thread_count` 仍为 3。
7. **select**：frame=3 个 fd_set；stdin/stdout/stderr 备份/恢复由 Carrier 成对管理。
8. **dirty/unknown**：有意不 free/不 close，整链保留到进程退出（无全局登记表，符合「无可写全局」）；多次 dirty 会增长 RSS/fd。

## 4. 高风险项（补验批优先）

1. pselect Carrier 按旧物理机制 `dup2` 覆盖任意低号 fd（含 0/1/2），真机必须验证 stdio 与进程其它 fd 恢复。
2. 旧 profile 参数（arm_hold/post_hold/punch 时序/nice 阶梯/超时）在冲刺实现中改为编译期常量或由 `operation_budget`/`timeout_ns` 推导，需真机校准；consumer nice 阶梯本轮未实现（仅固定 nice）。
3. TCP `struct tcp_zerocopy_receive` 首成员偏移 0 假设需反汇编/真机确认。
4. Placement 每轮 ≤400 child+401 fd；dirty 闭包增长；需真机测 fd/RSS 预算。
5. 失败注入矩阵、`/proc/self/fd`/线程/child 归零核验：全部未做。

## 5. UNVERIFIED（全部转 task-9 补验批）

- 真机冷机门禁：G4L-1 起每批、4M/4P/4T 各自单 Primitive（本机 `adb devices` 为空，不可执行；不得用 host/AVD 替代）。
- design-critic 复核 + native-core owner/borrow/access/terminal 逐项审查。
- 宿主失败注入、边界矩阵、cancel/partial-start 矩阵、PI 窗口反汇编白名单与调用图证据。
- Gradle 全门禁（compileDebugKotlin / assembleRelease / testDebugUnitTest）、`git diff --check` 全量、NDK 零告警、APK/DSO SHA、四事务产物生成 + 独立副本回滚。
- G4L-0 既判 FAIL 三项（B1/B2/B3）与 5 个先存红测试（PayloadRequestsTest×3、ScriptRenderTest×2）。
- Kotlin DTO 实值与 Native 常量对拍；G4L-5 wrapper 未实施（不在本指令内）。
- UML/评审记录回填。

## 6. 当前能力状态（不得混称）

- 九个 43499 入口：**已有物理实现**（不再 `UNSUPPORTED`），但**未经任何评审、门禁或真机验证**，属实验代码；**不得标 `supported`、不得接默认 Flow、不得进入 ksud 批、不得宣称真机可用**（`Flow43499Impl` 仍为 `cve43499-flow-not-wired`）。
- G4L-5/G4L-6：代码已实施（§7），但仍**未经评审/测试/真机验收**；G4L-6 生产启用（默认 Flow/ksud 接线）明确不做，等真机门 + 用户批准。
- 补验批：共享任务板 `task-9`（blocked_by task-3..8,10,11；当前未启动）。

## 7. G4L-5 / G4L-6 追加交付（2026-10-26 后续指令“冲刺到G4L-6”）

新增 Kotlin 11 文件、1974 行，**零修改既有文件**：

| 批次 | 文件（行数） | 内容 |
|---|---|---|
| G4L-5 | `backend/cve43499/owned/TokenOwner.kt`(348)、`PlacementOwner.kt`(358)、`PageOwner.kt`(207)、`ActorOwner.kt`(141) | private token + 单对象锁 + OPEN/TRANSFERRED/CLOSED；`callStatus==Int.MIN_VALUE` 才恢复 token；consumed 不回滚；clean 保留/dirty 消费；close 后/双 close/重复 execute 本地拒绝；无 Cleaner；转移链与 receipt 对拍；物理常量具名 |
| G4L-6 | `backend/cve43499/victim/{VictimProbe,ProcessVictim,ProcessVictimSession}.kt`(35/112/146)、`finish/{FinishHandoff,FinishAcceptance}.kt`(92/190)、`run/Cve43499Run.kt`(290)、`ui/screen/module/backends/FinishHandoffAdapter.kt`(55) | Kotlin `ProcessBuilder` victim（有界幂等 close、uid 超时清理）；不可变 handoff（路径+私有字节+SHA-256）；6 项证据验收（uid==0/脚本 size+SHA/ksud·apd/marker/SELinux=1/资源终结），缺一即具名拒绝；显式编排不接默认 Flow；UI adapter 复用既有 `toRootScript`+`FinishScriptStore` |

Lead 独立证据：
- `./gradlew :app:compileDebugKotlin :app:externalNativeBuildDebug --console=plain` → **BUILD SUCCESSFUL in 699ms，12 tasks（5 executed），EXIT=0**。
- `git status -- app/src/main/kotlin` 仅 11 个新增文件，零既有文件改动；`backend/**` 零 `import ui`；`Flow43499Impl` 仍 not-wired；token 全 private、无 Cleaner。
- G4L-6 未验证：真机 uid=0/脚本 SHA/ksud/marker/SELinux 读回、冷机门禁、宿主测试矩阵、R8/assembleRelease；ksud 证据仅文件存在性（不证明 late-load 成功）；生产入口尚未接线（按要求未改既有文件）。

