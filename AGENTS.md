# AGENTS.md — 流程 · 纪律 · 判据

<a id="e-review-rule-004"></a>

> **代号**：E-REVIEW-RULE-004（工程支撑－Agent 执行纪律－规则）；§1–§12 为本文条款，编号不改变教材优先及现有层级。 定义入口见[工程代号入口](/docs/development/documentation-standards.md#work-identifiers)。

> 项目正重构中，以用户要求为准

## 1. 最高思想与质疑义务（2026-10-07 用户裁定 · 强制）

`docs/development/软件工程守则.md` **是本项目的最高思想**，效力高于本文件及一切文档、计划、规范与个人偏好。

- **层级**：守则 > 本文件与其它文档（只能作**经验条目**；不得冲突/放宽/绕过/并列；冲突以守则为准，被冲突条目**待修订**）。
- **当责**：任何人（用户/开发者/Lead/子智能体）的要求、设计、决策或流程改动，若 ① 违反守则；② 破坏结构约束（阶段门、单变量、一处读取、单一权威、临时物零容忍、真机判据、产物身份核验）；③ 打断开发—设计—规划流程（越阶段门、无判据即继续、多线并行、改判据过门禁、以旁路替代设计）⇒ **当场质疑、拒绝执行、并教育提出者**（引条文 + 物证 + 冲突点与代价 + 合规替代）。**不得沉默服从、阳奉阴违、先做了再说。**
- **唯一例外**：用户**明确知情强制**越权 ⇒ 可执行，但**同批留沿革记录**（越过哪条/谁确认/代价）并登记技术债与偿还时机。**未经强制确认一律拒绝。**
- **反向义务**：合规要求必须高效执行，不得以守则为由拖延/扩范围/拒绝正常需求。**记录义务**：每次质疑/拒绝/教育/强制确认留一行可检索记录（时间·条文·结论）。

<a id="agents-change"></a>

## 2. 改动流程：先设计，后改动（强制）

<a id="agents-reading"></a>

**只读任务相关条款，复用未变化上下文，不遍历全部链接或历史。** 下列入口在触及对应任务时必须遵守；细则只在链接目标维护，按需读取不降低效力。**skill 是相应职责的按需入口，不是另一轮流程；细则仍只在所引规范维护。**

- **首次配置或缺工具**：按[开发环境](docs/development/development-environment.md)选择任务依赖，不把全套工具或产品门变成文档任务前置条件。

- **先设计、最小改动、适用验证**：按 [S/M/L 分级](/docs/development/engineering-standards.md#engineering-change-levels) 执行 Explore → Design → Implement → Verify；L 级先出计划并获用户认可。一批一类事，上批验过再进；同批门禁连续失败 2 次即停，回 debug。
- **作者先自检，再交付审查**：Lead/子智能体在设计/代码正式送审、交付前或接到自检任务时，主动读取并使用 [ghostlock-author-self-check](/.agents/skills/ghostlock-author-self-check/SKILL.md)，按 [作者自检](/docs/development/design-review-principles.md#review-author-self-check) 修正原交付；不另设报告或预审，不替代独立审查、批准和门禁。
- **任务选路**：普通 S/M 不跑全套；按已批准设计实施不重开设计；新 L／实质设计差异进入 OpenSpec，涉及实质架构再用 CDA。按[工具分工](docs/development/documentation-standards.md#planning-tool-authority)直达对应 skill，作者自检照常，独立审查只按 §7。
- **纠正立即生效**：停止受影响动作及委派，同步直接执行入口；不自行重启停止项，不把 NOT_RUN 改成 PASS。纠正、临时指令及交接按 [执行细则](/docs/development/engineering-standards.md#engineering-execution)。
- **不空转**：每轮推进唯一交付物或具名未知；无新输入/假设不重复检查，证据足够即交付。调查、SHA 和进展报告按 [取证与汇报](/docs/development/engineering-standards.md#engineering-investigation)；结论给命令与结果，不以状态复述冒充进展。

## 3. 代码修改与必要注释

编辑/删除代码或生成脚本时，按 [代码修改与注释判据](/docs/development/engineering-standards.md#engineering-code-editing) 执行；触及 legacy 或冻结组件时遵守 [明确保留边界](/docs/development/engineering-standards.md#engineering-preserved)。

## 4. 代码约定（规则；枚举见 introduction.md）

- native 依赖、全局状态与语言风格：[工程规范 §2.3–§2.4](/docs/development/engineering-standards.md#engineering-native-boundaries)、[§6.1–§6.3](/docs/development/engineering-standards.md#engineering-code-style)。
- 配置权威、版本与运行时数据：[§3.1](/docs/development/engineering-standards.md#engineering-configuration)；跨端合同改动还须遵守 [§5.1 双侧一致性](/docs/development/engineering-standards.md#engineering-cross-side)。
- 构建逻辑、工具链、manifest 与产物目录：[§6.5](/docs/development/engineering-standards.md#engineering-build)；命令直达 [§8.1](/docs/development/engineering-standards.md#engineering-gate-commands)。

## 5. 核心攻击代码审查（仅触及时执行）

触及 waiter/race/payload/route/exec 及其资源准备、持有、访问或回收时，必须执行 [资源所有权与终结顺序审查](/docs/development/engineering-standards.md#engineering-resource-review)，并保留 §8 真机门禁。

<a id="agents-collaboration"></a>

## 6. 子智能体委派与停止规则

- **可并行、自包含的工作优先委派；共享工作树只允许一条写入流，未指派者只读。** 委派与整合按 [协作细则](/docs/development/engineering-standards.md#engineering-delegation)，共享构建按 [排他规则](/docs/development/engineering-standards.md#engineering-build-writers)。
- **保全用户及他人的工作树改动。** 禁止 `checkout/restore/stash/reset/clean` 等改变工作树的 Git 命令及等效写回；不擅自恢复。
- **禁止冗余副本、流程包装与 rollback 准备/演练**，不论文件名或存放位置；正常产品产物、已有测试与必要原始门禁日志保留。编辑完整性及产物边界按 [文件操作细则](/docs/development/engineering-standards.md#engineering-file-operations)。

<a id="agents-review"></a>

## 7. 设计与规划审查岗 design-critic（2026-10-07 用户裁定 · 强制）

- **非作者、只读、同岗固定成员**；不写文件、不跑门禁。权威序：守则 > AGENTS > engineering-standards/design-philosophy > analysis。
- **触发**：新 L 级设计定稿，或实质改变已批准目标/范围、合同、owner/生命周期、失败语义、成功/验收判据，含审核/门禁原则修订。按已批准设计实施、进度/证据补录、格式/代号/链接机械修订不触发；实施偏离只审差异，普通代码评审与适用测试照常。
- **任务入口**：按上述条件触发时，Lead 委派既有非作者 critic 主动读取并使用 [ghostlock-design-critic](/.agents/skills/ghostlock-design-critic/SKILL.md)，无需用户另行指定。审查与关闭按 [审查原则 §3](/docs/development/design-review-principles.md#review-execution)：独立判断，critic 已指定且原条件明确的机械修复由 Lead 核对，其余定点复审；**阻断未按原条件关闭不得过对应 gate**，不无条件追加最终 PASS。

<a id="agents-gates"></a>

## 8. 验证门槛

- 按 [改动面](/docs/development/engineering-standards.md#engineering-verification-levels) 选门，执行 [现行命令](/docs/development/engineering-standards.md#engineering-gate-commands) 并按 [证据口径](/docs/development/engineering-standards.md#engineering-evidence) 留结果；纯文档不跑无关产品门。
- **攻击关键路径、设备依赖行为和端到端成功须真机证据**；执行 [真机条件与日志归档](/docs/development/engineering-standards.md#engineering-device-gate) 及 [归因规则](/docs/development/engineering-standards.md#engineering-causality)。新 profile 未过真机不得标 supported；设计审核不代表实现/真机通过。
- 开展实验须读 [实验纪律](/docs/development/engineering-standards.md#engineering-experiments)；`cmp_disasm` 仍是 [可选诊断](/docs/development/engineering-standards.md#engineering-diagnostic)，不是门禁。

<a id="agents-docs"></a>

## 9. 文档约定

触及文档、代号、结构图、双语或归档时，遵守 [文档规范对应条款](/docs/development/documentation-standards.md#documentation-task-map)；不改变计划内容、批准状态和门禁，不批量重编历史。

- **全局 UML 至少维护两个时间视点**：[当前状态](/docs/development/full-process-uml.md#global-current-view)只画当前工作树已经存在的结构与行为；[活跃计划最终目标](/docs/development/active-plan-target-uml.md#global-active-plan-target-view)画所有当前有效活跃计划按依赖顺序完成后的合并目标。目标图不得冒充已实现、已验证或已批准到 production；当前图不得混入尚未落地的目标。结构性源码改动同批更新当前图，活跃计划目标变化同批更新目标图；同批同时改变二者时两份都更新。
- 同一结构在**同一时间视点**只保留一处权威图；当前与目标不是冗余副本。确需第三份或更多全局 UML 时，必须在文件头声明时间视点、回答的问题、事实/计划来源及其与两份基础视图的关系，避免未标注的平行 authority。

<a id="agents-submit"></a>

## 10. 排障 · 提交

**完成一批改动并通过适用验证后，主动做范围明确的本地 commit，无需用户另行催促；push / 建 PR 仍由明确指令触发。** 提交按 [提交纪律](/docs/development/engineering-standards.md#engineering-commits) 执行，不夹带其他对话或未完成改动；仅遇工具链问题时读 [构建排障](/docs/development/engineering-standards.md#engineering-build)。

<a id="agents-pointers"></a>

## 11. 指针表（旧 → 新；旧标签 R9，非全局 R9）

- 三层架构（结构/CLI/资产/组件模型/owner 白名单/根级标量/vr_guard/命名空间）⇒ `introduction.md` §一 §三；决策 `adr/0001`·`0003`·`0004`·`0006`。
- 设计 ⇒ `npm run --silent openspec -- list --json`；执行与依赖 ⇒ `npm run --silent bd -- list` / `show <id> --json --brief-deps` 的 spec_id；语义代号 ⇒ [工程代号入口](/docs/development/documentation-standards.md#work-identifiers)；执行分层 ⇒ [P-RUN-X-001 简介](/docs/development/introduction.md#run-introduction)。
- 原则疑问 ⇒ [设计哲学对应主题](/docs/development/design-philosophy.md#e-review-rule-003)；全工程 R 短码 ⇒ [工程规则表](/docs/development/engineering-rules.md)，均按疑点查阅。

## 12. 通用日志

修改日志时必须遵守 [工程规范 §6.7：日志](/docs/development/engineering-standards.md#engineering-logging) 的级别、来源、写入与保密约定；当前运行 owner/调用链仍以所属已批准设计为准。
