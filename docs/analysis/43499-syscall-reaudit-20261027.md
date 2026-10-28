# 43499 真机前四路逐行/逐 syscall 重审汇总（2026-10-27）

- 状态：四路只读审计完成；**真机测试暂停**，先修阻断。未 commit/push。
- 快照：HEAD `a9af5482` + R1–R3e 未提交工作树（perf/enabled=0、三路 UNSUPPORTED）。
- 四路完整报告：Kotlin 编排（31 阶段表+5 专项）、Native syscall（A–H 表 + libc 清单）、perf 泄漏原语（两表+退役 seam 对照）、JNI 逻辑泄漏（44 函数清单+77 if 行号）。原文在会话记录；本件为阻断映射与修复顺序。

## 1. 阻断总表（按修复优先级）

| 序 | 编号 | 来源 | 位置 | 问题 | 最小修复 |
|---:|---|---|---|---|---|
| 1 | Kotlin-B4 | Kotlin 编排 | `debug-a301so.conf:207`+`FinishHandoffAdapter.kt:233-247`+`Flow43499Impl.kt:186-230` | 默认配置单 W1：无 verifyW1/W1b/W2/W3 却执行 finish → 设备留 permissive、acceptance 必拒 | 单 W1 加 verifyW1 且未验证不得进 finish；或 Gate readiness 拒绝该配置 |
| 2 | Kotlin-B5 | Kotlin 编排 | `FinishExecution.kt:53-58`；`Flow43499Impl.kt:882-894` | finish 由 App+`su -c` 执行，非 rooted victim；缺 KSU 前置守卫 | **task-45 已改**：victim 'G' handoff；补 `ksu_root_owned()` fail-closed |
| 3 | Kotlin-B6 | Kotlin 编排 | `Flow43499Impl.kt:882-894`；`FinishAcceptance.kt:188-192`；`FinishConfig.kt:61` | 缺 2s pre-dispatch settle + module/enforce 轮询；acceptance 要求 enforce==1 而脚本默认不恢复 | 补 settle+轮询；acceptance 改“恢复运行前 enforce 值” |
| 4 | Native-B1 | Native syscall | `actor.c:1073-1112,1252-1259`；`Cve43499Lifecycle.kt` | dirty/retained 后进程继续，PI worker 留活 → panic/复用风险（legacy `exit_group(70)`） | receipt 后 fail-stop（Kotlin kill/native exit_group）；最好整链 `:exploit` 进程 |
| 5 | Native-B9 / Kotlin-B5 | Native/Kotlin | 无 `ksu_root_owned` 检查 | KSU 已加载时直接打 → 预期内核 panic | Kotlin 开跑前检查 KSU 状态 fail-closed |
| 6 | Native-B2 | Native syscall | `carrier_pselect.c:495-516` | select 超时=总 deadline，4×200ms attempt 永不发生 | 新增 per-attempt select timeout（恢复 4×200ms）或改单窗口+operation_budget=1 并文档化 |
| 7 | Kotlin-B2 | Kotlin 编排 | `Flow43499Impl.kt:570-601`；`Cve43499WriteTransactionRunner.kt` | fast-repair 在 W2 后重跑完整 placement（≈1.4–1.5s），legacy 是预建修复页→fast 路由 | W2 前 prebuild repair 链（保持打开），W2 成功后只 prepareActor/executeActor |
| 8 | Kotlin-B1 | Kotlin 编排 | `Cve43499Lifecycle.kt:415-437`→`ProcessVictim.close()` | 杀 rooted victim → init_cred 引用递减 → 4 次后 panic | **task-45 已改**：rooted 只 detach（`retained=rooted-child-parked`） |
| 9 | Kotlin-B3 | Kotlin 编排 | `Flow43499Impl.kt:758-802`；`Cve43499WriteChain.kt:369-371`；`ProcessVictim.kt:94-95` | W3 三路均不可成功（leaf null/UNSUPPORTED、victim comm="sh"） | 先硬门 fail-closed 且不进 finish/不杀 victim；或补 'M' 探针+15 字符 comm |
| 10 | Native-B3 | Native syscall | 全路径无 setrlimit | 512 child+fd，soft limit 不足 → placement 直接失败 | 入口 getrlimit→setrlimit(NOFILE/NPROC, soft=max)，失败 SYSTEM_ERROR；门禁记录 `/proc/self/limits` |
| 11 | Native-B5 | Native syscall | `carrier_pselect.c:409-442` | prepare 阶段改写最多 ~250 个 JVM fd 且跨 JNI 返回 → ART/EBADF 风险 | fd install 移到 execute 首次 select 前（同一 native 调用）；首次门禁只跑 multicast |
| 12 | Native-B6 | Native syscall | `actor.c:307,360` | waiter/owner 新增 CPU pin，legacy 只 pin consumer | 首次门禁保持只 pin consumer（或单变量 A/B） |
| 13 | Native-B7 | Native syscall | `placement.c:437-441`；kernelsnitch 头 | KernelSnitch 扫描范围硬编码 64GiB，Kotlin scan/identity 只事后校验 | setup 增加 start/end 参数（或 validate 强制等于头文件常量） |
| 14 | Native-B8 | Native syscall | `perf.c:203-205` | perf pid=victim（跨任务）与 legacy self 相反 | G1 前重定语义；记录 paranoid/errno；默认 disabled 不变 |
| 15 | perf-B1 | perf | `Cve43499PerfSampleParser.kt:38`、asset `sample_type=16385`、测试 16385 | REGS_INTR 用了 1<<14（WEIGHT），应为 1<<18/262145 → 置 enabled 后必 24B 无 regs | **task-46 已改**：1<<18、asset 262145、测试+回归、撤回 10-08 结论 |
| 16 | perf-B2/B3/B4 | perf | parser/target source/无 androidTest | G1 缺证据（per-pass head/tail/unconsumed/records/ABI 分布/open errno）；环容量/pump 密度未测；App 跨任务权限零证据 | evidence 扩展 + `PerfMechanismProbeTest`；G1 实测后调 ring/pump；记录 paranoid |
| 17 | perf-B5–B9 | perf | `perf.c:352-367,225-247`；`jni43499.c:980-984`；`Cve43499PerfOwner.kt`；`TargetSource.kt` | close DISABLE 失败报 OK；open 清理失败仍 free；JNI open INCOMPLETE 泄漏 handle；owner 对 NOT_ACCEPTED 清 token；Failed 丢 closeRetained | **task-46 已列**机械修复 |
| 18 | JNI-L1 | JNI | `jni43499.c:507-518` | 用 3 个硬编码 JVM 类名 `IsInstanceOf` 选 primitive kind | Kotlin DTO 加 `primitiveKind`；JNI 只读字段；ABI 不变 |
| 19 | JNI-L2/L3/L4 | JNI | `jni43499.c:185-192,341-361,988-1027` | 翻译失败伪造 INVALID_ARGUMENT；空数组/OOM 分类；perf copy gate | 统一 NOT_ACCEPTED 不写 core 状态；OOM=SYSTEM_ERROR/ENOMEM；按 bytes_copied 拷回 |
| 20 | 文档 | 多处 | `jni43499.c:7`、`Native43499Jni.kt:326-328`、`actor.c:18-20`、`page_internal.h:58-68` | 注释称 core unavailable/UNSUPPORTED，实际已实现 | 同批修注释 |

## 2. 重要项（不阻断 G1 但需修/记录）

- Kotlin：`w1SettleUs` 死字段；缺 attempt>1 预验/末轮补验；缺 `in_direct_map` 护栏；identity 窗未按 iomem 收窄；pselect fd 劫持跨 finish；pselect 延时阶梯/attempts 退化；evidence 聚合不足；dead code/错位映射（`Flow43499Request.session`、`debugSession()`）；startVictim 非幂等；victimSnapshot 副作用；W1b 失败释放 quarantine；settleUs ms 截断；PV.close/uid 无互斥；resourcesReleased 证据过强；expectedUid 不可配置。
- Native：prepare→execute worker 自旋（consumer/punch）；RLIMIT；CPU pin；KernelSnitch 硬编码；perf 跨任务语义；KSU 守卫；SIGPIPE；pthread_create errno；multicast clock_gettime vDSO；inflight 无界；死参数（socket_buffer_size、actor priorities、pselect high_read/stdio_backup、placement detach API）；scan/operation 域溢出；retained 记账；abort fd 身份；`pselect` per-attempt delay 忙等。
- JNI：L5–L8 卫生（handle==0 前置、close/cancel bool、三态常量双份、priority 类型绕行、init_call_result 类型双关）。

## 3. 修复批次建议（真机前）

1. **P1 可达性/安全**：Kotlin-B4（单 W1 判据/Gate）、B5/B6（task-45 + KSU 守卫）、Native-B1（fail-stop）。
2. **P2 panic**：Kotlin-B1（task-45 rooted detach）、B2（prebuild repair）、Native-B2（pselect 超时）。
3. **P3 门禁前置**：Native-B3（rlimit）、B9（KSU）、B6（CPU pin 单变量）、B5（pselect fd 窗口）→ 先只跑 multicast 冷机门禁。
4. **P4 G1**：perf-B1（task-46）、perf-B2/B3/B4（evidence+androidTest+实测调参）、Native-B8（pid 语义）。
5. **P5 卫生**：JNI L1–L8、B10–B17、B20 文档；Native syscall 其它建议项。

## 4. 需要用户/Lead 裁定

- **Native-B1 fail-stop 形式**：Kotlin 收 receipt 后强杀 app 进程，还是 native `exit_group(70)`？是否把整链放独立 `:exploit` 进程？
- **Native-B2 pselect**：恢复 4×200ms（新增 per-attempt timeout 字段）还是保留单 800ms 并改名为单窗口（operation_budget=1）？
- **Native-B6 CPU pin**：首次门禁只 pin consumer（legacy 同形）还是保留三 pin 作为单变量？
- **w1Verify=unsupported 继续**（task-45 已按用户指令改）：是否接受“未验证继续 W1b/W2/W3”的风险（用户已直接指令，此处仅记录）。

## 5. 当前进行中/未提交

- task-45（routes-impl3）：finish 交 rooted victim + 拆 W1 墙（Kotlin）。
- task-46（r3e-impl）：perf B1 sample_type 修复 + B5–B9 契约修正。
- 全部改动未 commit/push；`perf.enabled=0`、三路 UNSUPPORTED。
