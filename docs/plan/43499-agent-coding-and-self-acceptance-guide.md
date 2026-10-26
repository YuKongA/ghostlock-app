# 43499 编码委派与自验收指南

- 日期：2026-10-26
- 级别：L（攻击关键路径、跨 JNI 契约、Native 资源所有权）
- 基线：`d9b180310ae35cf697843c64c7f2b26756036ef4` 加当前未提交的 G4L-0 修订
- 上位实施权威：[43499-resource-primitives-implementation-plan.md](43499-resource-primitives-implementation-plan.md)
- 批准状态权威：[43499-approval-status.md](43499-approval-status.md)
- 用途：供执行编码的 agent 逐批实施和自验；Lead 保留最终检查、批准、真机判定与进入下一批的权力。

## 0. 活跃计划核对与适用范围

本文件不另建第二套设计，不改变 G4L-0～G4L-6 的顺序、接口或批准状态。它只把上位计划翻译成可委派的
写范围、交付物和自验规则。发生冲突时按“软件工程守则 → AGENTS.md → 上位实施计划 → 本指南”处理。

当前只批准了 G4L-0。G4L-1及以后每批都必须先由批准状态表登记“用户已批准本批”，Lead才能发开工
消息；否则编码agent只能只读勘察。Lead终验只验当前批，不能批准下一批；G4L-4T/G4L-6仍是独立批准点。

明确冻结：`legacy/`、`legacy2/`、`very_legacy/` 只读；`general`、43284、64560、UI、handoff 脚本、
`LegacyProfileConverter.kt`、profile 与 payload/address 计算均不得顺手修改。不得 commit、push、stash、
checkout、restore、reset、clean，也不得新增 CLI/环境变量/临时生产入口。

## 1. 角色、单 writer 与交接

| 角色 | 权限 | 必须交付 | 禁止 |
|---|---|---|---|
| Lead | 冻结批次、最终审查、串行门禁、真机与批准判定 | 冻结快照、终审意见、最终验证记录 | 把 agent 自报 PASS 当最终批准 |
| coding-agent | 一次只写一个获批批次 | 源码、测试、自验报告、资源表、原始输出路径 | 扩批、改判据、替 Lead 批准下一批 |
| design-critic | 只读设计审查 | 编号发现、总判、已验证/未验证 | 写代码、跑门禁、修改计划 |
| native-core reviewer | 只读攻击资源审查 | owner/borrow/access/terminal 逐项核对 | 与 writer 同时覆盖文件 |

同一时刻仅一个 coding-agent 写工作树。Lead 分派消息必须含：批次、冻结 HEAD/工作树哈希、允许写文件、
只读证据文件、禁止范围、命令、预期结果、停止条件。agent 收到的范围之外全部冻结。

共享 `build/**` 和 Gradle 门禁只能由 Lead 串行运行。agent 可以先做不写共享构建树的语法/纯测试；如需
运行 Gradle，必须等 Lead 明确给出构建时段。判断 writer 是否结束看 agent 状态，不看 mtime。

## 2. 所有批次共用的编码协议

### 2.1 开工前

agent 必须先完成并报告：

1. 阅读 `docs/development/软件工程守则.md`、`design-philosophy.md`、`engineering-standards.md`、本指南与上位计划。
2. `git status --short`，区分开工前用户改动；逐个记录允许写文件 SHA-256 和行数。
3. `git log --all -- <允许写路径>`，以及上位计划指定的 legacy 物理顺序锚点。
4. 输出“我不会改”的冻结清单；发现范围冲突立即停，不自行扩大。
5. 把本批每个资源列成：创建者、唯一 owner、临时借用者、访问区间、终结点、释放者、失败终态。

### 2.2 写码规则

- 原有代码不得直接覆盖：先用有界注释保留原文，写 `TODO(reason + 完成判据)`，再写新码；同批门禁通过并经
  Lead 确认后立即删除旧码注释和 TODO，不得把临时块带入基线。机械改动可直接改，但报告中逐项标明。
- C/C++ 新增或触及的公开/内部函数使用 Doxygen，至少写目的、参数、返回、失败语义、前提、阶段；Kotlin
  使用 KDoc。禁止 `using`；不新增可变全局；不以 magic 读取已释放 token。
- 每个入口先验证输出 ABI，再验证输入 token/参数；只有输出 ABI 有效且 token 非零才接受 owned token。
- Native 不读取 profile，不推导 route，不计算最终地址，不生成/解释 payload，不做 retry/fallback。
- 所有 syscall、fd、mapping、child、thread、PI 关联都必须有成对失败路径；辅助资源不得发生 UAF。
- 一次批次只保留一个真实强定义；`api_unavailable.c` 对已实现符号同步递减，禁止双定义或静默 fallback。

### 2.3 每次返回前的 agent 自验

agent 只有同时满足以下条件才可写“SELF_ACCEPT=PASS”，否则必须写 FAIL/STOP：

- `git diff --check` 为 0；允许写路径外无新增 diff；无构建产物被纳入交付 diff。
- 新码编译零 warning；测试命令退出 0；完整 literal output 保存到具名日志，而非只摘最后一行。
- 对每个新增 guard 做至少一个负例，证明 guard 确实拒绝错误输入；负例撤回后无残留。
- `nm/readelf` 证明公开符号唯一；JNI/Kotlin 名称与字段对拍；除 const 外无新增 43499 全局状态。
- 成功、clean failure、dirty/indeterminate、cancel、early return、partial-start 全部有测试或明确列为真机待验。
- 资源表逐项闭合；任何“未证明可释放”的闭包只能进入 retained，不得猜测清理。
- agent 明确列出“未验证”，不得用 host/AVD/非零 token/requeue 代替真机效果。

agent 自验通过只表示“可交 Lead 复核”，不表示批次通过、设计获批、生产可用或 route supported。

## 3. 分批编码任务卡

以下均为候选范围，不构成写授权。每批共同前置为：上一批Lead终验PASS、design-critic阻断0、批准表已登记
用户批准本批、Lead冻结封闭的绝对路径清单。最终只以该清单为准；需要新增任何未列路径（含私有头、测试、
UML、门禁记录）时立即STOP，由Lead重新冻结范围后继续。

### 3.1 G4L-1 Placement

**目标**：只实现 `prepare/cancel/closePlacement`，返回复制事实；不实现 Page、Actor 或 Primitive。

**候选范围（不构成写授权）**：

- 新增 `app/src/main/cpp/gl43499/core/placement.c` 及对应内部私有头（确有必要才加）；
- 修改 `app/src/main/cpp/CMakeLists.txt` 与 `core/api_unavailable.c`，仅移除三个 Placement stub；
- 新增独立 host 测试/失败注入测试；按结构变化同步 UML 和本批 device-gate 记录。

**实现不变量**：对象独占 children、memfd、staging、scan/measurement 资源；Kotlin 传入所有边界、数量、CPU、
timeout；Native 只验证非零、范围、对齐、溢出和明确物理上限。partial-start 严格按
`stop → join → unmap/close → reap child`。cancel 停止并排空但不消费 token。close 的未接受/已接受语义遵循 ABI。

**自验矩阵**：ABI 过小/版本错/reserved 非零；0/1/MAX 计数；上下界反转、步长零、乘加溢出、CPU 越界；
每一个资源创建点失败注入；cancel before/during/after prepare；close clean 与 retained；重复调用只在 wrapper
层拒绝，Native raw 重复 token 不作为受支持测试。验证 `/proc/self/fd`、线程、child 在 clean 路径回到基线。

### 3.2 G4L-2 Landing 与 Page

**前置**：同时满足本节共同前置。

**目标**：实现 `landPage/closePage`，冻结 preflight/commit；不准备 Actor。

**候选范围（不构成写授权）**：`page.c`和私有结构、CMake、unavailable中两个Page符号、Page单测、
commit-point 失败注入和 Kotlin/JNI receipt 对拍测试。

**实现不变量**：JNI 在进入 Native 事务前深拷贝最终 bytes；Native 不解释 payload 业务含义。precommit 失败
`placement_consumed=0`；commit 后 success/clean failure/dirty/indeterminate 均为 1。成功只产生一个 Page
token；Page 独占被内核访问所需的 mapping/fd/child/socket 物理闭包。closePage 只接收未转给 Actor 的 Page。

**自验矩阵**：空/超长 bytes、长度与几何不符、对齐和溢出、每个 preflight/commit/postcommit 失败点；逐项
验证 receipt 与 token；commit 后不得重新使用 Placement；clean 路径资源归零，unknown 路径完整 retained。

### 3.3 G4L-3 Actor 与线性接管

**前置**：同时满足本节共同前置，并由design-critic复核资源顺序。

**目标**：实现 Page→Actor 接管、Carrier 抽象、三个 joinable worker、屏障、cancel/close；尚未验证的真实
Primitive 必须返回 `UNSUPPORTED`。

**候选范围（不构成写授权）**：`actor.c`、`actor_internal.h`、中性`carrier_internal.h`及测试；只修改Actor对应的
unavailable 符号和构建清单。不得把 route/controller/session 搬回 Native。

**冻结顺序**：以 `legacy2/core/race.cpp:361-363,413-415` 和
`docs/analysis/43499-43284-resource-ownership.md:10-14,59-61` 为证据。任何 stop、解除关联、join、释放顺序
差异都先停批，另立设计和真机批准。

**自验矩阵**：第 N 个同步对象/线程/Carrier 资源创建失败；Page clean failure 不消费，提交成功或 dirty
消费；execute×execute、execute×cancel、execute×close 由 wrapper 串行约束；worker 每个阻塞阶段 cancel；
close 证明“停止访问 → 同步/解除 PI → join → Carrier → Page”。线程卡死或引用未知时整体 retained。

### 3.4 G4L-4M / G4L-4P / G4L-4T Primitive

三批严格独立，固定顺序不代表可并行写。每批只新增一个 `PrimitiveSpec` 分支的 Carrier 实现和测试；不得
自动 fallback 到另一 Primitive。M/P 各自需用户逐批确认；T 仍是独立批准点。

每个 agent 必须先提交“legacy source:function → 新 source:function → 输入/输出/errno/effect → owner/终结
→ 差异 → 验证”的逐 symbol 表，Lead 批准后才写实现。PI 窗口内禁止 kind 读取、switch、函数指针/BLR、
JNI、锁、分配、日志、首次触页与未批准 syscall；prepare 阶段完成所有 dispatch 和预触页。

**每个 Primitive 的自验**：资源创建点失败注入；直接调用形状检查；窗口反汇编白名单；cancel/partial-start；
clean/dirty闭包；单 Primitive 冷机真机。一个 Primitive 的 PASS 不外推另两个。

### 3.5 G4L-5 Kotlin/Lua wrapper

**目标**：每资源一个 `AutoCloseable` owner class，private token；业务 route 仍由 Kotlin/Lua 组合。

**候选范围（不构成写授权）**：由Lead预先冻结具体Kotlin与测试文件；JNI holder保持transport，不再包一层Access。Access是
独立读写原语，不拥有 Page/Actor，也不 prepare 完整攻击。

**不变量**：每个方法用对象锁串行；close/转移前原子取 token 并置 0；未接受失败恢复；receipt consumed=1
不恢复；`use {}` 只负责当前 owner；禁止 Cleaner 猜测回收 retained。Lua 可反射选择公开 Access/JNI 原语，
raw token 误用不承诺 fail-closed。

**自验矩阵**：正常转移；每种 clean failure 保留；每种 dirty failure 消费；close 后调用；双 close 在 wrapper
拒绝；异常/coroutine cancellation 的 finally；R8 字段 retained/旧字段 absent；JNI/Kotlin 九入口对拍。

### 3.6 G4L-6 victim、Finish、ksud

本指南不自动授权本批。开工前必须确认至少一个 Primitive 已过真机门、用户另行批准、UI handoff 脚本契约
已冻结。victim 继续 Kotlin `ProcessBuilder`；UI 生成包含 root 管理器启动和 SELinux 恢复的 handoff 脚本；
Native 不生成、选择或解释脚本。最终验收同时要求 uid、脚本对象身份、ksud/marker、SELinux 恢复及所有
资源终结，不允许用 terminal 输出替代。

## 4. coding-agent 返回格式

agent 返回必须逐字包含以下章节，缺一项视为未交付：

```text
BATCH=<G4L-x>
FROZEN_HEAD=<commit>
SELF_ACCEPT=<PASS|FAIL|STOP>

CHANGED_FILES
- <absolute path>: <why>

UNCHANGED/FROZEN
- <path or subsystem>

OWNERSHIP_TABLE
resource | create | owner | borrower/access window | terminal | releaser | failure state

BEHAVIOR_DIFF
- before:
- after:
- explicitly unchanged:

COMMAND_EVIDENCE
COMMAND=<exact command>
INPUT=<exact fixture/device/build>
LITERAL_OUTPUT=<literal or log absolute path + sha256>
EXIT_STATUS=<integer>
RESULT=<PASS|FAIL>

NEGATIVE_TESTS
- injected fault -> expected status/effect/consumed -> observed

UNVERIFIED
- <host cannot prove / device gate pending>

RISKS_AND_STOP_CONDITIONS
- <remaining item>
```

另外必须在Lead冻结的**仓库外批次证据目录**交四件：`MODIFIED_FILE`、`DIFF_FILE`、`VERIFICATION.txt`、
可执行`ROLLBACK.sh`，报告绝对路径和SHA。`MODIFIED_FILE`仅是受测副本，不是第二份生产源；rollback只操作
独立副本，禁止`checkout/restore/stash/reset/clean`及等效写回。四件不得提交进仓库，Lead须重开并核对仓库无残留。

## 5. Lead 最终检查清单

Lead 不复用 agent 的结论，只复用其原始日志。每批依次执行：

1. **身份**：冻结 HEAD、agent diff、允许写清单、构建输入和 APK/DSO SHA 对得上；工作树无第二 writer。
2. **范围**：逐文件审 diff；范围外变化为 0；旧码注释/TODO/临时探针/旁路为 0。
3. **契约**：C header、JNI 翻译、Kotlin DTO、R8 keep、UML、批准表只有一处权威且字段/状态一致。
4. **资源审查**：逐资源验证创建→owner→访问→停止→终结；辅助 UAF 为 0；dirty 不被 clean。
5. **原子窗口**：检查 commit point、consumed receipt、PI 顺序和反汇编；不得用函数名替代调用图证据。
6. **独立重跑**：串行运行上位计划 §6 全门禁，读取全部 XML；保存 literal output、exit 和 hash。
7. **rollback**：在另一副本实际执行；比较全部目标文件和行为，不只看脚本退出码。
8. **真机**：G4L-1起按冷机、固定 CPU、单 Primitive、KernelSU 未加载运行并归档；环境不符不判 PASS。
9. **非作者评审**：design-critic 只读复核；阻断必须为 0；coding-agent 逐条回应。
10. **裁决**：Lead 只可给 `PASS / FAIL / BLOCKED-BY-EVIDENCE`；只有 PASS 才能请求进入下一批。

Lead 的最终记录必须复述 exact command/input/literal result/exit，列出已验证与未验证，并明确当前入口是
`UNSUPPORTED`、实验、还是已过真机的 supported；三者不得混称。

## 6. 可直接发送给 coding-agent 的任务模板

```text
你负责 43499 的 <BATCH>，这是唯一写入流。先读仓库 AGENTS.md、软件工程守则、
43499-resource-primitives-implementation-plan.md 和 43499-agent-coding-and-self-acceptance-guide.md。

冻结基线：<HEAD + working-tree hashes>
允许写：<absolute file list>
只读证据：<legacy/document anchors>
明确禁止：允许写清单外全部文件；尤其 legacy/general/43284/64560/UI/profile/payload/address policy。

目标：<one batch objective>
不变量：<ownership, commit point, cleanup order>
必须测试：<batch matrix>
共享 Gradle/build 门禁由 Lead 最后串行运行；你先完成独立语法、host、失败注入与静态证据。
连续失败两次、发现顺序证据冲突、需要扩范围或无法闭合资源表时立即 STOP，不改判据。

按本指南 §4 原样返回，并创建、测试、重开四个事务产物。你只能报告 SELF_ACCEPT，不能批准批次或下一批。
```

## 7. 本指南的验收

- 与上位计划无新增架构决策；批次顺序和独立批准点不变。
- 每批均给出写范围、冻结项、不变量、负例、自验和停止条件。
- agent 自验与 Lead 终验严格分离；最终责任和真机裁决保留给 Lead。
- design-critic 阻断为 0 后，本指南才可用于实际委派。

## 8. 评审记录

| 时间 | 快照 | 总判 | 阻断 | 备注 |
|---|---|---|---:|---|
| 2026-10-26 | 初稿 | 待评审 | — | 只定义委派与验收，不授权 G4L-1 |
| 2026-10-26 | 修订稿`515ab0c9…` | 通过使用gate | 0 | 重要0、建议0；不授权后续批 |
