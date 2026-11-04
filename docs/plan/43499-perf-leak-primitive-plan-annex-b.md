# 43499 perf 泄漏原语 L 设计 · 附件 B（CBN / 风险 / 裁定记录与实现批义务）

- 主体：[43499-perf-leak-primitive-plan.md](43499-perf-leak-primitive-plan.md)（唯一设计权威；本附件只承载明细，不另立结论）。ABI/解析/证据/生命周期/影响文件见 [附件 A](43499-perf-leak-primitive-plan-annex.md)。
- 日期 2026-10-27（修订 3：H1/H2/H3=ABA 用户裁定已并入）；作者 writer r3e-impl（task-43）；快照 HEAD `a9af5482`+R1–R3d 未提交工作树。
- 沿革：Lead 2026-10-27 曾批准主件/附件 16 KiB 配额；2026-11-03 用户已取消，现只按职责边界组织。

## F. CBN 明细

### F1 CBN-1 采样上下文（决策点：perf 采样器在哪执行）

| 备选 | 收益（可度量） | 成本（含边际） | 翻转条件 |
|---|---|---|---|
| **A) App 进程跨任务（本设计，已选）** | victim 改动=0 文件/0 协议；复用既有 uid probe 接口；Native 仅 3 入口 | 需 G1 证 App 域跨任务权限（**当前零证据**）；采样质量依赖 pump；每 pass 1 次 open/collect/close | G1 失败（H2-b 下仍保持默认关闭） |
| B) victim 侧自采样 native/JVM helper（legacy 保真） | 无跨任务权限依赖；寄存器语义与 legacy 同（自采样） | 新可执行体+victim 协议（victim/ 冻结⇒**需单独批准**）；打包/调试/检测面成本高（可执行 `lib*.so` 有 43284 先例） | A 失败且用户批准 victim 侧改造 |
| C) 放弃 W2/W3 | 0 风险/0 成本；单 W1 保留 | 失去 uid0 目标能力 | 用户接受放弃凭据写 |

**结论**：选 A；H2-b 使其「先落实现、G1 后补」；B 为唯一保真替代但需单独批准；C 为 fail-closed 兜底。

### F2 CBN-2 Native/Kotlin 分工（决策点：record 解析放哪）

| 备选 | 收益 | 成本（含边际） |
|---|---|---|
| **A) Native 仅机制+raw 拷贝、Kotlin 解析（本设计，已选）** | 满足用户硬约束；解析零设备可单测、可复现；新 sample 类型只改 Kotlin | 每 pass 一次 ≤ring 大小拷贝（≤256KiB，可忽略） |
| B) 退役形态（Native 解析+投票，`perf_ring.hpp`/`perf_vote.hpp`≈204 行） | 少一次拷贝 | 违反用户「Native 零解析」硬约束；解析负例只能设备测 |

**结论**：A（硬约束；B 仅在用户改裁定时考虑）。

### F3 CBN-3 目标源 seam（决策点：R3d `fun interface` 返回 `Long?` vs 具名结局）

| 备选 | 收益 | 成本（含边际） |
|---|---|---|
| **A) `acquire(context): Cve43499TargetAcquisition`（本设计，已选）** | 失败原因可检索（要求）；改动面=1 接口+Flow 2 处+测试 5 处（grep 实测） | 新失败类型加一个 sealed 分支 |
| B) 保留 `Long?`+第二接口回传 reason | 不动旧签名 | 双读取路径（E5 高危、A1 违例） |
| C) 保留 `Long?` 丢弃 reason（现状） | 0 改动 | 失败不可定位（违反 D6） |

**结论**：A；R3d 未 commit/无生产接线（`BackendRunGate.kt:166` 默认 null）⇒同批演化不构成回退。

## G. 风险明细

| 风险 | 影响 | 缓解/判据 |
|---|---|---|
| 权限不可用（paranoid>1 且无 CAP_PERFMON / 跨任务 ptrace 拒 / seccomp） | perf 路径不可用 | G1 前置；具名 fail；W2/W3 保持 UNSUPPORTED；默认关闭 |
| ABI=0 记录（10-08 探针 WEIGHT 位错，已修正） | 无候选 | `perf-registers-not-delivered` 具名；不猜地址；262145 待 G1 |
| 候选不可信（10-06 非对齐低票 / mondrian 非对齐写不落） | 地址错⇒W2 写内核任意对象=**panic 残余风险** | **用户 2026-10-27 H1-a 已书面接受**；实现批记录+W2 uid 前置校验+票数门槛+2-of-3+无重试；对齐/SP 默认关（G1 后收紧） |
| 环溢满/样本丢失 | 数据不可信 | `unconsumed>data_size` 判 overflow；ring_pages/pump 行数配置；G1 调参 |
| 采样扰动时序 | W2 失败 | 采样在 W1b 后、close 后按 `perf.settle_after_acquire_us` settle；W2 attempts 语义不变 |
| VR tag/常量错 | 地址错 | 单一源=`execution.leakTag*`；单测向量 |
| 检测面 | `perf_event_open` 在 audit/logcat 留痕，无内核日志 | 风险低；不新增打印、不写目标文件 |
| LMK/被杀 | fd/mmap 泄漏至进程退出 | close 覆盖取消路径；retained 证据 |
| 地址生命周期 | victim 重启/退出后地址失效 | 采样后 `victim.probe` 校验 starttime；W3 restarter 缺失=1 轮（沿用 R3d） |

## H. 裁定记录（ABA）与实现批义务

### H1 用户裁定 H1-a（原 B3）：对齐/SP/投票门槛/panic（**方案 B 统一口径**）

- **裁定**（2026-10-27「ABA」；approval §6.6 同载）：G1 前不硬拒绝对齐/SP（`require_*` 默认 0，G1 后收紧）；票数门槛 ≥64/比例 ≥0.25；一致性 **3 pass「2-of-3」**；**用户书面接受残余 panic 风险**。
- **方案 B（R1）**：2-of-3 选中地址 A 后，门槛按**同意 pass 聚合**：`agree_votes=Σ votes(p)(winner_p==A)`、`agree_candidates=Σ candidates(p)(winner_p==A)`、`threshold=max(64, ceil(0.25×agree_candidates))`；**判定顺序**=①infra/parser 失败（open/collect/pump/parser）立即 abort；②无 2-of-3 地址⇒`perf-agreement-failed`；③`agree_candidates<64`⇒`perf-insufficient-candidates`；④`agree_votes<threshold`⇒`perf-votes-below-threshold`；⑤（可选）对齐/SP；⑥Acquired。**低票异地址 pass 不 abort、不参与门槛**（10-06：p1=A(149)/p2=B(34,非对齐)/p3=A(162)⇒A 选中，agree_votes=311、agree_candidates=512、threshold=128⇒Acquired）。
- **采样基线证据（R2）**：`Cve43499VictimContext` 增 `baseline: Cve43499VictimBaseline(pid,startTimeTicks,uidProbe)`（Flow 采样前捕获，uidProbe 为一次 `victimUid()` 结果）；`Cve43499TargetAcquisition.Acquired(address, evidence)` 的 evidence 显式携带 baseline + perf 统计（passes/每 pass winner+votes+candidates/2-of-3/agree_votes/agree_candidates/threshold/alignmentOk/spEvidenceOk）。
- **W2 uid 前置校验（Kotlin-only；baseline vs 当前）**：W2 取址后、首笔写前：①当前 `victimSnapshot()` 的 pid+startTimeTicks == baseline（不等/缺失⇒`w2-precheck:victim-identity-changed`）；②当前 `victimUid()` 非空、≠0、== baseline.uidProbe（否则⇒`w2-precheck:uid-baseline-unexpected`）；③地址策略证据全过（否则⇒`w2-precheck:target-untrusted`）；④victim 存活。失败⇒fail-closed 不写/不重试，结果进 evidence；通过后 W2 attempts/写后 uid=0 判据不变。**残余风险**：无内核读原语，前置校验无法证明内核目标正确→panic 残余风险按 H1-a 书面接受。
- **测试向量（R1/R2）**：10-06 三 pass 方案 B 采信；低票异地址 pass 不 abort；infra/parser 失败立即 abort；判定顺序 ③先于④；采样后 victim 重启（starttime 变）⇒identity-changed；pid 复用但 starttime 不同⇒identity-changed；采样后 uid 变化⇒uid-baseline-unexpected；baseline uid=0/null⇒uid-baseline-unexpected；happy+evidence 断言。

- **候选/门槛算例（E2）**：2 pass ×256 候选⇒`agree_candidates=512`、`threshold=max(64, ceil(0.25×512))=128`；两 pass 各 63 票⇒`agree_votes=126<128` 拒；各 64 票⇒`agree_votes=128≥128` 过；比例边界 0.24⇒`ceil(122.88)=123`、0.25⇒`ceil(128)=128`；`insufficient` 用两 pass 各 31 候选⇒`agree_candidates=62<64`⇒`perf-insufficient-candidates`；与 10-06 的 `agree_votes=311/agree_candidates=512/threshold=128` 一致。

### H2 用户裁定 H2-b（原 B4）：G1 门禁位置

- **裁定内容**：知情豁免「先落实现、G1 后补」——实现默认关闭（`perf.enabled=0`）；G1（A301SO 冷机：App 跨任务权限/寄存器投递/reg31/0x200/**3 pass 2-of-3**）为**生产启用硬门**；技术债 owner=Lead，偿还=P0c。
- **实现批义务**：R3e-1..4 可落代码但默认关闭；R3e-5 生产启用必须 G1 PASS 且另批批准；G1 未过不得标 supported、不得启用默认路径；在 approval/review 记录豁免与债。

### H3 用户裁定 H3-a（原 B5）：无状态句柄

- **裁定内容**：Native 不检查 OPEN→COLLECTED→CLOSED；close 唯一性由 Kotlin owner 保证；Native 仅 open/collect/close 机械资源操作。
- **落地**：API/所有权表述见附件 A §A3/§A4/§D；删除全部 handle 三态/重复 collect=BAD 表述；负例改为「重复 collect 机械返回（无状态错）」；重复 close/close 后使用=不支持调用（Kotlin owner 契约）。
- **owner 合同测试（R4；H3-a 正确性证据）**：Kotlin owner 包装（如 `Cve43499PerfSession`）：open→collect→close 恰好一次 native close；open→close（未 collect）释放；close 后 token 清 0 且 owner 层拒绝后续 collect/close（不触 JNI）；二次 close 不产生第二次 native close；collect 失败后 `finally close`；断言 fake Native 的 open/collect/close 调用序与计数。

### H4 验证命令与判据明细

- **离线命令**（literal+期望）：`./gradlew :app:compileDebugKotlin` EXIT=0（新文件 warning=0）；`./gradlew :app:testDebugUnitTest --tests "com.ghostlock.app.backend.cve43499.*" --tests "com.ghostlock.app.core.flow.*"` EXIT=0 且 XML 全量三桶（基线 48+新增，0 fail/0 err）；`./gradlew :app:externalNativeBuildCleanDebug :app:externalNativeBuildDebug` 分两次 EXIT=0、warning/error grep=0；`python3 tools/scan_native_literals.py --gate r2` EXIT=0（unclassified/target/residue=0、新 allowlist 重签）+`--signature`；`clang -std=c11 -I app/src/main/cpp/gl43499/core tools/abi_layout_probe.c -o /tmp/abi_layout_probe && /tmp/abi_layout_probe`（既有 20 项不变+新 4 项 128/32/48/80）；`git diff --check` EXIT=0。
- **G1 条件/步骤/证据/判据**：条件=A301SO 冷机、KernelSU 未加载、固定 CPU 0/1、单 route multicast、debug 构建、`perf.enabled=1`；覆盖=App 跨任务 perf 权限/寄存器投递/reg31/0x200/**3 pass 2-of-3**；步骤=W1→W1b→perf 获取（evidence：`/proc/sys/kernel/perf_event_paranoid`、open errno、每 pass head/tail/unconsumed/records/ABI 分布/候选/votes/spVotes/winner、2-of-3 结果、agree_votes/agree_candidates/threshold、baseline+W2 precheck、close 回执）→W2（前置校验+uid）→W3（阻断 B，UNSUPPORTED）→收尾；判据=W1/W1b 与基线一致、perf 证据齐全、W2 `victimUid=0`、无 panic、资源终结（retained=0 或登记）；G1 未过⇒`perf.enabled` 保持 0、W2/W3 保持 UNSUPPORTED、不换路。

### H5 实现批义务清单（登记）

1. 记录 panic 接受（approval §6.6 H1/H2/H3 行；实现记录同批复述）。
2. 落地 W2 uid 前置校验（四分支+测试+evidence）。
3. `perf.enabled=0` 默认；release 缺 debug 资产拒绝沿用现有 fail-closed。
4. G1 后按证据把 `require_alignment`/`require_sp_evidence` 收紧为 1（配置切换；须重跑 G1）。
5. G1 未过不得启用/不得 supported；技术债 owner=Lead、偿还=P0c。
6. 更新 approval/review/v2 plan 与本设计 sha；scanner allowlist 重签。
7. **R5 说明（非用户例外）**：perf attr 未列字段=机制零值（非目标/容量/协议参数），由 Kotlin 交叉校验；若与「全部参数化」字面冲突由 Lead 再报用户。

## I. 实现记录（R3e-1..3，task-44，2026-10-27，工作树未 commit）

- **R3e-1 Native（无状态 3 入口）**：`core/perf.h`(NEW，146 行)+`core/perf.c`(NEW，377 行)：`openPerfSampler/collectPerfSamples/closePerfSampler` 对应的 `glk43499_perf_open/collect/close`；ABI 4 结构=128/32/48/80（`_Static_assert`+host probe 纳入）；既有 20 结构 sizeof 不变；`api.h`/`CMakeLists.txt`/`jni43499.c` 接线；`tools/abi_layout_probe.c` +4 项。**纯机械证据**：perf.c 无 `switch`/循环/状态字段（`grep "switch|for (|while ("` 为空；`state` 仅注释与回执枚举 `RESOURCE_OPEN/CLOSED`），只做 open/mmap/ioctl/整块 memcpy/tail/close。
- **R3e-2 Kotlin**：`perf/Cve43499PerfSampleParser.kt`（280/24、272/279/281 invalid、绝对 head/tail、回绕/跨环、tag 还原、投票）、`Cve43499PerfTargetSource.kt`（方案 B：2-of-3 + 同意 pass 聚合 `threshold=max(64,ceil(0.25×agree_candidates))` + 判定顺序 + 对齐/SP 默认仅 evidence）、`Cve43499PerfNative.kt`（JNI seam）、`Cve43499PerfOwner.kt`（close 唯一/token 清零/close 后拒绝）、`flow/Cve43499WriteChain.kt`（`Cve43499VictimBaseline/Context/TargetEvidence/TargetAcquisition` + `acquire`）、`Flow43499Impl.kt`（baseline 捕获、pump、W2 precheck、perf evidence）、`A301SoDebugConfig.kt`（`perf` 段可选解析；`perf.enabled=0` 默认）、`FinishHandoffAdapter.kt`（仅 debug `perf.enabled=1` 构造 target source）、`debug-a301so.conf`（`perf` 段 `enabled=0`）。
- **门禁（literal+exit，实跑）**：`:app:compileDebugKotlin` **EXIT=0**（warning/error=0）；过滤单测 `:app:testDebugUnitTest --tests "…cve43499.*" --tests "…core.flow.*"` **EXIT=0**，XML 全量 **79 tests / 0 fail / 0 err**（基线 48 + R3e 31：parser 11、target 11、owner 4、W2 precheck 3、debug 配置 2）；`:app:externalNativeBuildCleanDebug`+`:app:externalNativeBuildDebug` 两次 **EXIT=0**，warning/error=0（35 个 .o，含 perf.c.o）；`python3 tools/scan_native_literals.py --gate r2` **EXIT=0**（files=21 literals=1552 allowed=1552 unclassified=0 target=0，residue 全 0；**allowlist 重签 sha256=`841d6915…`**）；host `abi_layout_probe`：既有 20 项不变 + 新 **128/32/48/80**；`git diff --check` **EXIT=0**。
- **默认关闭证据**：debug 资产 `perf.enabled=0`（仅 debug source set；`app/src/main/assets/profile/43499/` 无该文件）；未启用路径 `build()` 不创建 target source ⇒ `writeChain=null`（单 W1/W2-W3 UNSUPPORTED 不变）；单测覆盖「缺 perf 段⇒null」「enabled=0⇒`perf-config-invalid`（不触 Native）」。
- **与设计的实现差异（均已登记）**：W2 precheck 的「当前 uid」取 `victimSnapshot().uid`（生产实现内部即 uid probe，避免额外 probe 消耗且满足 R2 基线对比）；evidence 字段落地为 `perfPasses/perfPassAddresses/perfAgreeVotes/perfAgreeCandidates/perfThreshold/perfAlignmentOk/perfSpEvidenceOk/perfCloseRetained`；owner 类名 `Cve43499PerfOwner`（close 返回 receipt）。
- **未 commit/push**：工作树改动；真机 G1 未跑（见 §H4/§I 末）。
- **仍 UNVERIFIED（G1 前不虚报）**：A301SO App 跨任务 perf 权限/寄存器投递/reg31/0x200/3 pass 2-of-3；`perf_event_paranoid` 实际值；pump 样本密度/门槛可达性；G1 失败后产品结论。`perf.enabled` 保持 0、不得标 supported。


## J. 审计修复记录（task-46，2026-10-27，工作树未 commit）

- **B1（必修）**：`PERF_SAMPLE_REGS_INTR` 由误写 `1<<14`（=WEIGHT）改为 **`1<<18`**
  （单一常量+注释禁写 1<<14；保留 WEIGHT 常量仅供回归）；TargetSource expected 由
  该常量派生；debug asset `sample_type=262145`、`flags=8`(CLOEXEC)；测试断言 262145
  并新增「仅 1<<14⇒`perf-config-invalid`」回归；10-08 finding 撤回「寄存器不投递」
  （探针 WEIGHT 位错致 24B/ABI=0；原始输出保留；262145 判据=280B/ABI=1）；
  主件 §3/§5 与附件 A §B/§C③ 同步为「探针 bit 错已修正」。
- **B5–B9（机械）**：close DISABLE 失败⇒`SYSTEM_ERROR/DIRTY/retained=0`；open 清理
  不净⇒**不 free**、`SYSTEM_ERROR/DIRTY/QUARANTINED/retained=1`；JNI open 回执写失败
  先 close 已建 handle 再 -1；owner 仅非 `NOT_ACCEPTED(-2)` 清 token；TargetSource
  `Failed/Missing` 携带 `closeRetained` 并由 Flow W2 失败 evidence 透传（不改进程）。
- **门禁（树绿 18:33:33，routes-impl3 最新写后）**：
  `:app:compileDebugKotlin` EXIT=0；过滤单测 EXIT=0、XML **109 tests/0 fail/0 err**
  （本 task 直管范围 54/0/0）；NDK CleanDebug+Debug EXIT=0/0 warning；scanner r2 EXIT=0（literals=1574、allowed=1574、
  unclassified/target/residue=0；allowlist 重签 **`7724a69b…`**）；ABI probe 新增
  128/32/48/80 且既有 20 项不变；`git diff --check` EXIT=0。**Native 仍纯机械**：
  perf.c 无 `switch`/`for`/`while`/state 字段，B5–B7 只改错误回执/资源记账。
- **状态**：`perf.enabled=0` 保持；未 commit/push；G1/262145 寄存器投递 **UNVERIFIED**。
