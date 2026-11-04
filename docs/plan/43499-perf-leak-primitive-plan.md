# 43499 perf 泄漏原语 L 设计（R3e · 机制-only Native + Kotlin 解析）

- 状态：**L 设计 修订 3（已实现）**。方向已获用户批准（[approval-status](43499-approval-status.md) §6.6「方案 1批准」）；critic R3e 复核 B1–B5/I1–I4/S1–S2 已修订；**H1/H2/H3 用户裁定「ABA」并已记 approval §6.6**（§12）；实现待第三轮复核阻断 0 + 批准（AGENTS §2）。**本批零实现写入**。
- 日期 2026-10-27；作者 writer r3e-impl（task-43）；快照 HEAD `a9af5482`+R1–R3d 未提交工作树；解除 [review](43499-three-route-line-review-20261027.md) §6.4 阻断 A 的设计入口，不取代任何计划。
- 参考：legacy `legacy/attack/ops.cpp:434-513`、very_legacy `main.c:844-965`、退役 seam（`git show d5a46686:…/perf_{seam,seam_linux,ring,vote}*.hpp`）。
- **文档组织（I2）**：2026-11-03 用户取消旧 16 KiB 配额；主件与附件只按职责边界分工。附件：[A](43499-perf-leak-primitive-plan-annex.md)（ABI/解析/证据/生命周期/影响文件）、[B](43499-perf-leak-primitive-plan-annex-b.md)（CBN/风险/裁定记录与实现批义务）。附件不另立结论。

## 0. 结论摘要

1. 只新增一个**采样资源**：Native 三入口 `openPerfSampler/collectPerfSamples/closePerfSampler`，只做 `perf_event_open/mmap/ioctl/整块复制/释放`，**不解析记录字节**。
2. 采样器在 **App 进程**（Flow 所在），开 `pid=victim_pid, cpu=-1` per-task 事件；victim 仍是 Kotlin 启动的 `sh`，**协议/代码零改动**（pump 复用 `probe→id -u`）。
3. 解析/投票/2-of-3/校验全 Kotlin；不可信即 `write-targets-missing:<原因>`；**实现默认关闭（`perf.enabled=0`），G1 为生产启用硬门（H2-b）**。

## 1. 目标、范围、不变量

**目标**：取 victim 内核 `task_struct` 基址，经 `Cve43499WriteTargetSource` 交 W2/W3 planner，替代 legacy victim 内 `perf_find_task()`（legacy2 同形 `attack.cpp.disabled:443-522`）；解 W2/W3 P0c 阻断 A（review §6.4:171-172）。**范围**：Native 采样机制 + Kotlin 解析/目标源 + debug 配置 + 测试 + 真机门禁计划。

不变量：**I1** W1-only 路径/字符串不变（`writeChain=null`；`Flow43499Impl.kt:171-172`）；**I2** 三路 generator/W2/W3 布局语义不变（review §6.1 R3d-2）；**I3** Native 零业务逻辑/零状态机——**无状态句柄（H3-a）**，close 唯一性由 Kotlin owner 保证；**I4** ABI v4 既有结构零改动/`struct_size` 严格；**I5** victim/ 冻结、不改 legacy/legacy2/very_legacy；**I6** 不在 PI 窗口内 syscall；**I7** 不加旁路/CLI、不 commit；**I8** 主件与附件只按职责边界组织，不设字节或篇幅门。

## 2. 机制-only Native 边界

**允许（穷举）**：① `perf_event_open`（attr 全来自请求；未列字段=机制零值，附件 A §A5）；② `mmap` ring；③ `ioctl(ENABLE/DISABLE)`；④ 只读环元数据（geometry/`data_head`/`data_tail`，**非 sample 解析**）；⑤ acquire fence+环数据区**整块** `memcpy` 到调用方缓冲；⑥ `data_tail=data_head`（读游标，legacy 同形 `ops.cpp:497`）；⑦ `munmap`→`close`。另加 ABI/域校验（附件 A §A4）。

**禁止（评审可据此否决）**：解析 record header/`PERF_RECORD_SAMPLE`/ABI 字/寄存器；候选收集、窗口过滤、tag 还原、投票、平票规则；地址选择/对齐/区间校验；pass 一致、重试、退避、超时、重放；errno→业务原因翻译（原样写 `system_error`）；默认值/容量上限/目标常量；状态校验（H3-a）；日志。**理由**：退役树把 ring walk/vote 放 Native（`perf_ring.hpp`/`perf_vote.hpp`），本设计按用户「Native 不做解析」搬进 Kotlin；Native 面≈机制调用+拷贝。

## 3. 执行上下文与权限

| 项 | 决定 |
|---|---|
| 采样进程 | **Flow 所在进程（App，untrusted_app/app uid，`Dispatchers.IO`）**；Native43499Jni 同进程（`BackendRunGate.kt:166`） |
| 采样目标 | victim 子进程主线程 `pid`，`cpu=-1`（`ProcessVictim.kt:66,68-75,92-98`；`Cve43499Lifecycle.kt:477-486`） |
| 权限关系 | 同 uid 父子：跨任务 open 需 `ptrace_may_access(PTRACE_MODE_ATTACH_REALCREDS)`=同 uid+dumpable+parent（Yama 后代）；kernel 语义，**G1 实测** |
| SELinux/链路 | 采样在 W1 成功（permissive）后；W2 step 入口、W1b 后：无开放写链、无 PI 窗口（A2-1:44,145；`Flow43499Impl.kt:171-176,525-527`） |
| seccomp | App 是否放行 `perf_event_open` 未知，**G1 由 errno 判别** |

**attr config**：15 请求字段全由 Kotlin 显式给值；其余 attr 字段=机制零值（清单/交叉校验=附件 A §A5）。

**pump**：复用 `victimUid()` 现有 `probe→id -u` 流（`ProcessVictim.kt:66,68-75,92-98`），**不新增 victim 命令**；每 pass=`open→pump(N 行,预算 B ns)→collect→close`（N/B=debug 配置）；失败⇒`perf-pump-failed:*`，不重试。

**W2 uid 前置校验（H1-a，实现批必做；Kotlin-only）**：W2 取址后、首笔写之前校验：①victim 身份不变（pid/starttime）；②uid 基线非 0（写后 uid=0 才可归因）；③地址策略证据（票数门槛/2-of-3/`require_*` 按配置）；④victim 存活。任一失败⇒`w2-precheck:*` fail-closed，不写/不重试；结果进 evidence（明细附件 B §H）。

**设备证据（I4；四列/反例/零证据=附件 A §C）**：现有证据全为 **shell/app 自采样（pid=0）**：10-03 A301SO 成功且 W2 后 `child uid=0`（A2-1:96-97,137-138）；10-06 非 0x200 对齐 34/256 票候选靠换轮恢复（20261006-015621:99-108,146-147）；10-08 探针 bit 错致 abi=0（已修正）；mondrian app 可开 perf 但候选非对齐、写不落（:32-34）。**「App 跨任务」A301SO 零证据**（G1 首证点）。

## 4. ABI（append-only；ABI v4 不变）

新增 `core/perf.h`（NEW；公共头不得 include Linux 头，保证 host probe 可编译），4 个新结构 sizeof=**128/32/48/80**（**4 项全纳入 static_assert+probe**；逐字段附件 A §A1–A2）；close 复用 `glk43499_close_result`（48）；**既有 20 结构 sizeof 全不变**。C 原型/out/out_capacity/无状态语义/close 接受点=附件 A §A3–A4；attr 字段+机制零值=附件 A §A5；close 失败表=附件 A §A6。JNI/Kotlin：新增 `@Keep` `Native43499PerfOpenRequest`（15 字段全必填）、`Native43499PerfOpenResult`/`Native43499PerfCollectResult`（+6 字段），复用 `Native43499CloseResult`；新 extern `openPerfSampler/collectPerfSamples/closePerfSampler`；三态协议复用（`jni43499.c:20-36`），output ByteArray 只在 core 返回后复制（`jni43499.c:5-6`）。**参数化**：Native 无新增目标/容量/协议数值宏；scanner `--gate r2` 须重签 allowlist（sha256 记 approval），`BANNED_MAGIC_TOKENS` 不得命中。

## 5. 生命周期与所有权（明细附件 A §D）

**无状态句柄（H3-a）**：perf handle 是不透明 token，Native impl 只存 fd/mmap/geometry，不存/不校验 OPEN/COLLECTED；`openPerfSampler` 创建（每 pass 一个）、`collectPerfSamples` 借用（可重复调用=机械重读 head/拷贝/tail，无状态错）、`closePerfSampler` 由 Kotlin owner 在 finally **唯一一次**调用；close 后 token 不得使用（重复 close 属不支持调用）。fd/ring mmap 由 Native impl 独有，close 顺序=`munmap`→`close`；JNI 临时缓冲 core 返回后 free；地址值只经 `run.victimTaskAddress` 交 W2/W3。munmap/close 失败⇒DIRTY+`retained_resource_count=1`+不 free/handle 已消费，不影响已得地址（附件 A §A6）。失败/取消⇒`finally close`，无 Cleaner/无全局注册。**PI**：仅在 W2 step 入口前后（W1b `closeChain` 后）调用，Flow 测试断言「pump 时 openChains==0 且无 executeActor 进行中」；Native 不在 actor 线程/`executeActor` 内调用；不把 perf syscall 放进 PI 内（`pi-window-constraints.md` §3）。**并发**：单线性 owner、同 handle 不并发。

## 6. 失败语义（具名 fail-closed）

所有失败拼成 `cve43499:write.w2:write-targets-missing:<reason>`（Native 只回 `call_status`/`system_error`，errno→reason 映射在 Kotlin）。reason 家族：①open=`perf-open-denied/unsupported/failed`；②机制=`perf-mmap-failed`/`perf-enable-failed`/`perf-collect-failed`/`perf-buffer-too-small`；③parser=`perf-no-samples`/`perf-registers-not-delivered`/`perf-sample-abi32-unsupported`/`perf-record-invalid`/`perf-ring-overflow`/`perf-ring-geometry-mismatch`/`perf-no-candidates`；④默认启用=票数门槛（`perf-insufficient-candidates`/`perf-votes-below-threshold`）+identity 窗；**默认关闭=对齐/SP**（记 evidence `alignmentOk/spEvidenceOk`；配置开启后失败⇒`perf-address-untrusted:alignment`/`:sp-evidence`）；⑤`perf-agreement-failed`/`perf-pump-failed:<reason>`/`perf-cancelled`；⑥W2 前置=`w2-precheck:*`。

**不回退**：不写固定地址、不复用历史泄漏、不重试（≤3 pass 只用于 2-of-3 一致性）；W3 沿用 `run.victimTaskAddress`（`Flow43499Impl.kt:614-615`）不二次采样，W3 失败不触发重新获取（restarter 另批）。legacy「换轮重试」（10-06:102）属 victim 重生成（阻断 B），本设计不做。

## 7. Kotlin 解析（修订 3；全表附件 A §B）

解析全部 Kotlin 纯函数 `parseAndVote(ringView(bytes,head,tail), config)`（无 Android/JNI/时间；配置 data class）。**要点**：期望长度=8+8+8+`register_count*8`（仅 ABI=1）⇒**280B**；ABI=0 合法=**24B**；由 `sample_type`/`register_count` 推导（不硬编码；272/279/281⇒invalid）；`data_head/tail` **单调绝对位置**，`unconsumed=head-tail_before>data_size` 才 overflow（**`head>data_size` 正常回绕必须通过**）。**H1-a 策略（唯一方案）**：票数门槛默认启用=**2-of-3 选中地址按同意 pass 聚合**：`max(64,ceil(0.25×agree_candidates))`（顺序见附件 A §B3）；一致性=**≤3 pass、任意两个相等（2-of-3）**（全不同⇒`perf-agreement-failed`）；**对齐/SP 默认关闭**（`require_alignment=0`/`require_sp_evidence=0`，仅 evidence），G1 后收紧。**新增假设**：0x200/SP reg[31] 非 legacy（反例 10-06/mondrian）。**能失败向量**（附件 A §B4）：280/24/272、wrap vs overflow、63/64 票与 0.24/0.25 比例、候选<64、2-of-3 四组合、tag 还原、10-03 真值、对齐/SP 开关、fake Native/finally、Flow seam/acquire 一次/openChains==0/w2-precheck。
## 8. 验证与真机门禁计划（命令/判据明细=附件 B §H4）

**离线**：`compileDebugKotlin` EXIT=0；过滤单测（cve43499.*+core.flow.*）EXIT=0+XML 三桶（48+新增，0/0）；NDK clean+debug 两次 EXIT=0 零告警；`scan_native_literals.py --gate r2` EXIT=0（unclassified/target/residue=0、allowlist 重签）；`abi_layout_probe`（20 项不变+新 4 项 128/32/48/80）；`git diff --check` EXIT=0。
**Native 机制负例（设备侧⇒host harness 债）**：`androidTest/.../PerfMechanismProbeTest.kt`（debug-only，App 进程）：不存在 pid（ESRCH）、`ring_pages=0`（INVALID_ARGUMENT）、out 容量不足（INVALID_ARGUMENT 且可 close）、**collect 两次机械返回（H3-a 无状态错）**、无权限 errno（G1）；探针留作门禁 harness（owner=Lead、偿还=P0c）。
**真机门禁 G1（生产启用硬门 H2-b；A301SO 离线⇒缺席即 UNVERIFIED/技术债）**：条件/证据/判据明细=附件 B §H4；核心=App 跨任务权限/寄存器投递/reg31/0x200/2-of-3 全部实测；W2 后 `victimUid=0`、无 panic、资源终结；**G1 未过不得启用（`perf.enabled` 保持 0）/不得 supported**；失败即停（归档、不猜、不换路），受限设备⇒W2/W3 **UNSUPPORTED**、单 W1 不受影响、不 fallback。
## 9. CBN 与风险

CBN 明细（收益/成本/边际/翻转全表）见 [附件 B §F](43499-perf-leak-primitive-plan-annex-b.md)。结论：**CBN-1 选 A=App 跨任务**（victim 改动 0、复用 uid probe；B=victim 侧自采样 helper 需单独批准；C=放弃 W2/W3 兜底）；**CBN-2 选 A**（Native 仅机制+raw 拷贝、Kotlin 解析；退役 Native 解析≈204 行违反「Native 零解析」）；**CBN-3 选 A**=`acquire(context): Cve43499TargetAcquisition`（1 接口+Flow 2 处+测试 5 处；R3d 未 commit/无生产接线⇒同批演化不回退）。

**风险（逐条/反例=附件 B §G）**：权限不可用⇒具名 fail、W2/W3 UNSUPPORTED；寄存器不投递⇒`perf-registers-not-delivered`；**候选不可信⇒W2 写任意内核对象=panic 残余风险（用户 2026-10-27 H1-a 已书面接受）**，实现批记录+W2 uid 前置校验+无重试；环溢满⇒`unconsumed` 判据/调参；采样扰动⇒W1b 后+settle；VR tag/常量⇒单一源+单测；检测面低；LMK⇒close 覆盖取消/retained；地址生命周期⇒既有 starttime probe、restarter 缺失=1 轮。

## 10. 批次、影响文件、文档/approval、明确不做

**批次（实现另批；本批只设计）**：R3e-1 Native 机制+JNI+ABI；R3e-1b 设备机制探针；R3e-2 Kotlin 解析器+目标源+seam+Flow W2 context/pump+W2 uid 前置校验+debug 配置；R3e-3 机器门禁+单测；R3e-4 真机 G1；R3e-5 生产启用=G1 PASS 后硬门（H2-b），另批批准。实现批落地 `perf.enabled=0` 并记录 panic 接受。

**行为差异（实现后）**：仅 debug `perf.enabled=1` 且注入 `writeTargetSource` 时启用多写链；默认行为/字符串不变。新增=3 JNI 入口/perf reason/evidence 键/目标源新签名/W2 前置校验；既有入口/ABI/错误码/布局零变化。

**影响文件（实现批 write scope；本批不动）**：Native=`core/perf.{h,c}`(NEW)、`core/api.h`、`jni43499.c`；Kotlin=`jni/Native43499Jni.kt`、`backend/cve43499/perf/*`(NEW)、`flow/{Cve43499WriteChain,Flow43499Impl}.kt`、`A301SoDebugConfig.kt`、`FinishHandoffAdapter.kt`；资产 `debug-a301so.conf`；测试 3 新/2 改+androidTest 探针；工具 `tools/{scan_native_literals.py,abi_layout_probe.c}`；全清单附件 A §E，其余冻结（C1）。

**文档/approval**：本件=唯一设计权威；实现批同批更新 approval §6.6/review/v2 plan。本批已完成：approval §6.6「ABA」引用+§6 索引+文档组织记录；review §6.4 设计入口（修订 3）。**不 commit**。**明确不做**：不改 legacy/legacy2/very_legacy；不在 Native 加解析/选择/状态机/重试/状态校验；不改 victim/；不新增其他泄漏机制；不改既有 ABI/错误码；不启用生产默认（`perf.enabled=0`）；本批不写实现代码；不碰 KernelSnitch/43284/UI。

## 11. UNVERIFIED（第三轮复核与 G1 核对）

1. A301SO `perf_event_paranoid` 实际值；10-03 shell 成功未区分「paranoid≤1」与「shell 有 CAP_PERFMON」。
2. **App（untrusted_app/app uid）跨任务 `perf_event_open` 在 permissive 下可用性**（ptrace/SELinux/seccomp）——G1 仲裁；当前零证据。
3. 寄存器组在 App 进程是否投递（10-08 为探针 WEIGHT 位错；262145 探针未跑、paranoid 未记）。
4. `reg[31]==current task_struct` 与 0x200 对齐均为**新增假设**（非 legacy；反例 10-06/mondrian）；G1 后按证据决定是否把 `require_alignment`/`require_sp_evidence` 收紧为 1。
5. pump 样本密度/pass 稳定度、ring_pages/pump 行数/票数门槛在真实样本上的可达性（G1 调参）；G1 失败后的产品结论（CBN 翻转由 Lead/用户决定）；实现工作量为估计。

## 12. 用户裁定记录（ABA，2026-10-27；approval §6.6 同载）

- **H1-a**（方案 B）：对齐/SP 默认 0（G1 后收紧）；门槛=2-of-3 选中地址的**同意 pass 聚合**（详见 approval §6.6 H1-a/附件 B §H1）；**用户书面接受 panic 风险**；实现批记录+W2 uid 前置校验。
- **H2-b**：先落实现、G1 后补；默认关闭（`perf.enabled=0`）；G1（A301SO 冷机：跨任务权限/寄存器/reg31/0x200/2-of-3）为**生产启用硬门**；技术债 owner=Lead，偿还=P0c。
- **H3-a**：**无状态句柄**——Native 不检查 OPEN→COLLECTED→CLOSED；close 唯一性由 Kotlin owner 保证；Native 仅 open/collect/close 机械资源操作。
- **实现批义务**：记录 panic 接受；落 W2 uid 前置校验+测试；`perf.enabled=0`；G1 未过不得启用；更新 approval/review/v2 plan/sha。
