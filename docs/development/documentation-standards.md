# 文档编写规范（Documentation Standards）

<a id="e-doc-rule-001"></a>

> **代号**：E-DOC-RULE-001（工程支撑－文档－编写与编号规则）；跨端模板是本根附录，不另立规范。 定义入口见[工程代号入口](#work-identifiers)。

> 本仓库一切文档的写法。风格参照 Google Developer Documentation Style Guide：短、直接、面向读者；
> 内容分类参照 Diátaxis。来源见文末。
> 效力与冲突处理见 [E-REVIEW-RULE-004/§1：最高思想与质疑义务](../../AGENTS.md#e-review-rule-004)；编号仅定位，不另设权威层级。
> 沿革：2026-11-03 用户裁定取消文档篇幅配额；现行完整口径见[篇幅与拆分](#documentation-length)。

<a id="documentation-task-map"></a>

**按任务定位**：新写/结构整理读“先定类型、怎么写”；涉及代号读[工程代号](#engineering-identifiers)；涉及结构、双语、计划/门禁记录、归档分别读[画图](#documentation-diagrams)、[双语](#documentation-bilingual)、[模板](#documentation-templates)、[流程](#documentation-process)。仅笔误/链接修订不触发模板全填或全文复读。规则仍适用于相关改动，沿链接无需遍历全套文档。

## 先定类型

写之前回答两个问题：**给谁看？回答什么问题？** 然后在表里选一类，一篇文档只属于一类。

| 类型 | 写什么 | 放哪里 |
|---|---|---|
| 教程 / 快速开始 | 带人跑通一次 | `README.md`、`docs/profile/README.md` |
| 操作指南 | 完成一个任务 | `docs/development/*` |
| 参考 | 字段、命令、结构 | `docs/profile/PROFILE_SCHEMA.md`、`defaults.md`、`core/src/README.md` |
| 解释 | 为什么这样设计 | `design-philosophy.md`、`engineering-standards.md`、`docs/analysis/**`（历史） |
| 决策记录（ADR） | 一次架构决策：背景/决策/备选/后果 | `docs/analysis/adr/NNNN-*.md` |
| 计划 | L 级改动开始前 | OpenSpec proposal/design/specs；任务与实际证据在Beads；[分工](#planning-tool-authority) |
| 门禁记录 | 真机验证之后 | `docs/analysis/device-gates/`（归档，永久保留） |
| 发布说明 | 每个版本 | `docs/release-note-*.md`（归档） |

同一内容只写一处，别处链接；已有同类文档时更新它，不新建平行版本。

<a id="planning-tool-authority"></a>

## 设计与执行：OpenSpec + Beads + CDA

| 信息 / 动作 | 唯一位置与入口 |
|---|---|
| 全工程治理、审查与门禁原则 | 本套现行规范；工具模板不复制、放宽或覆盖 |
| 目标、范围、非目标、当前承诺与后续触发 | OpenSpec `proposal.md` / `design.md`；[propose](../../.agents/skills/openspec-propose/SKILL.md) / [update](../../.agents/skills/openspec-update-change/SKILL.md) |
| 真实调用者、取舍、owner/失败语义、图与逐文件scope | 同一 OpenSpec `design.md`；共享设计只定义一处。作者按需使用 [CDA](../../.agents/skills/constraint-driven-architecture/SKILL.md)，不是另交报告 |
| 行为规范与验收定义 | `openspec/specs` 的当前公共合同、change 的 `specs` 增量及 `design` 验收矩阵；已批准但未实现/未验收的增量不提前同步主规范 |
| 执行任务、认领、真实依赖、实时进度 | Beads；[apply](../../.agents/skills/openspec-apply-change/SKILL.md)从 `spec_id` 读取 OpenSpec，不维护 `tasks.md` 或另一份checkbox |
| 原批准、纠正、独立审查及实际命令/结果 | 原 Beads 的 notes/comments；保留日期、原话/来源、范围、输入与取代关系。设计受纠正时同步OpenSpec当前边界，证据不重写为新批准 |
| 探索、合同同步、完成后归档 | 按需用 [explore](../../.agents/skills/openspec-explore/SKILL.md)、[sync-specs](../../.agents/skills/openspec-sync-specs/SKILL.md)、[archive](../../.agents/skills/openspec-archive-change/SKILL.md)，不是每批必走的额外串行流程 |

**旧纯Markdown计划已完整迁移，不恢复 MASTER/ROADMAP/REMAINING 或旧计划正文 authority。** OpenSpec 持有完整有效设计，不能只放一个指向旧计划或Beads design的壳。Beads `spec_id` 直达对应文件/稳定锚点，description只概括执行对象，design留空，acceptance_criteria只指向适用验收定义；不复制完整设计、scope或矩阵。原批准和历史证据中的引用/当时设计说明是只读沿革，不是并行可编辑设计。

先从相关Beads任务或[语义代号登记](#work-identifiers)直达OpenSpec，按问题读取所需章节；不默认展开全部change、依赖或归档。旧号映射只在[公共执行设计](../../openspec/changes/runtime-foundations/design.md#legacy-identifiers)维护；其它仓库任务仅链接，不导入第二份。

`ready`/artifact `done`只说明依赖或工件状态，不表示批准、实现或运行PASS；Beads decision.closed可能只是设计收录，不是整个产品变更验收。原批准范围、冻结/暂停、真机门与最终树复验条件保持；实际未执行仍为NOT_RUN。合理后续需求仍deferred，具名条件缺失仍blocked；依赖只连接实际先决条件，参考链接不设新阻断。

作者自检与非作者独立审查继续按[AGENTS §2、§7](../../AGENTS.md#agents-review)的原触发，直接检查同一OpenSpec正文和Beads证据，不生成额外报告或最终PASS轮次。Beads CLI是任务唯一写入口，JSONL是生成的交接投影；安装、按字段读写及导出见[开发环境 §5](development-environment.md#planning-tools)。

## 怎么写

- **短**。删掉不改变读者行为的重复句；具体做法优先于抽象描述，完整性按下述篇幅规则保留。
- **直接**。主动语态、现在时；写"运行 X"，不写"X 可以被运行"。
- **可核对**。数字、命令、日志名照实写；结论不超出证据。不写"应该没问题 / 大概 / 看起来正常"。
- **可追溯**。引用历史用 `git show <commit>:<path>`；计划与门禁记录必须有 commit 和日期。
- **不过度承诺**。不用"无缝、完美、彻底"这类词。
- 术语统一：`route`/`profile`/`gate`/`handoff`/`waiter`/`payload` 不译；中文用全角标点，
  代码与英文术语用反引号。
- 规范类文字用 必须/应当/可以（MUST/SHOULD/MAY）；其他文档不用命令语气。
- 英文文档按 Google style：active voice、second person、present tense、sentence case 标题。
- 链接用相对路径；跨文档引用给文件链接、完整代号与中文标题，必要时定位稳定锚点；不用裸章节号或裸局部字母号代替身份。

<a id="documentation-length"></a>

### 篇幅与拆分（2026-11-03 用户裁定）

任何文档、计划、分析、门禁记录与附件均不得以字节数、页数、行数或篇幅设准入/准出上限；只按职责、读者与权威边界拆分，且不得为缩短文档删减判据、证据或沿革。运行时协议、Binder、资源与载荷的容量上限不属于文档长度限制，保持各自合同。“短、直接、一处权威”是质量要求，不是长度配额。

<a id="engineering-identifiers"></a>

## 工程代号：全局短码、局部复合码

代号用于快速识别主题并直达定义，不承载完整变更清单。下列语法是本项目对公开实践的裁剪，不是国际标准规定；不新增编号服务、TSV 注册表、校验器或审批岗位。

### 全局与局部

- **全局短码 `R<n>`**：仅指 [engineering-rules.md](engineering-rules.md) 的全工程规则，例 `R1：单一权威`；该表是 R 语义的唯一来源。全局表示适用范围，不表示最高权威：教材仍高于经验规则，教材条文继续引用具名章节，不为编号改写教材或另造“最高守则”。
- **局部根代号 `<类别>-<主模块>-<范围>-<序号>`**：如 `P-LOG-IO-001：日志文件输出`，前三段可反推产品/工程、主责模块和主要合同领域，末段区分同一作用域的不同工作。计划和局部规范均可有根代号；对象类型、状态、版本和负责人写在正文，不再塞进代号。
- **局部子项 `<根代号>/<类型><序号>`**：新增条目用 `/需求01`、`/决策01`、`/批次01`、`/问题01`，标题同时写中文含义；同一根的同类型连续分配，复审和另开报告不重置。问题分配先查该根的既有审核记录；在根文档的已有审核位置保留“问题号→定义锚点”链接，不另建问题台账。普通段落用 1、1.1，不给每段分配永久代号。
- **活跃旧号迁移为语义主代号**：旧 D1/R4C 等不能仅套统一计划前缀。按条目实际主责模块与合同领域分组，使用如 `P-PAYLOAD-CORE-001/决策01`、`P-LOG-X-001/批次01`；标题同时写中文主题。一个文件可定义多个语义根，不因此拆文件/新建计划；同一合同下连续决策/批次共用根，真正不同领域使用不同根。旧号只在 OpenSpec 公共执行设计的映射及原证据中保留，旧锚点在归档仍可达；不重新批准、排序或重写历史。规范的旧 A–G 等仍按自身根定位，与产品决策不混用。

### 唯一短词表

词义按固定槽位读取，模块是稳定职责而非目录名。只有实际出现新职责且现有码确实不适用时才补一行；禁止每份计划自创缩写或预建未来模块。

| 槽位 | 代号 | 含义 |
|---|---|---|
| 工程类别 | P | 产品功能与其运行架构 |
| 工程类别 | E | 构建、工具、规则和文档等工程支撑 |
| 主模块 | UI | 跨功能的用户界面与交互组织 |
| 主模块 | SETTINGS | 应用设置与持久化配置 |
| 主模块 | RUN | 执行编排、会话与 RunHost 合同 |
| 主模块 | BACKEND | Backend 描述、成员资格、私有实现与接线边界 |
| 主模块 | DIAG | 诊断功能及其实际执行链，不表示 root/攻击能力 |
| 主模块 | LOG | 日志采集、呈现与导出 |
| 主模块 | PROFILE | 配置档格式、解析与验证 |
| 主模块 | PAYLOAD | 用户载荷定义与执行语义 |
| 主模块 | FINISH | 收尾动作与恢复语义 |
| 主模块 | BUILD | 构建、打包与产物生成 |
| 主模块 | REVIEW | 设计审核、规划边界与门禁规则 |
| 主模块 | DOC | 文档组织、引用与命名 |
| 领域/范围 | ICON | 语义图标与图标目录 |
| 领域/范围 | UI | 用户界面与交互 |
| 领域/范围 | CORE | 模块内部模型与编排逻辑 |
| 领域/范围 | IO | 文件、流与持久化合同 |
| 领域/范围 | IPC | 进程间通信与 wire 合同 |
| 领域/范围 | NATIVE | native 构建、绑定与执行边界 |
| 领域/范围 | RULE | 工作规范与验收规则 |
| 领域/范围 | X | 由具名主模块负责的跨模块/跨层合同，正文必须列出边界，不代表“全仓任意修改” |

P-LOG-IO-001：日志导出（[gl-s7q设计](../../openspec/changes/logging-runtime/design.md#record-gl-s7q)，`p-log-io-001`）、P-LOG-X-001：跨层日志中枢（[gl-s7q设计](../../openspec/changes/logging-runtime/design.md#record-gl-s7q)，`p-log-x-001`）、[P-RUN-IPC-001：RunHost wire](../../openspec/specs/runhost-wire/spec.md#p-run-ipc-001) 均为已有内容的实际身份；`E-BUILD-NATIVE-001` 仍仅为未分配示例。`CORE` 指模块内部合同，`IO` 指文件/流，`X` 只用于确有具名 owner 的跨层/跨模块合同，不作为不分析归属的兜底；其它影响仍列在原范围中。测试随被验证合同归属，不因 test 字样自动成为工程工具任务。

### 分配、定位与兼容

1. **一处入口**：复用[本页代号登记](#work-identifiers)：根代号、中文标题、定义链接。每个根只登记一次；文档头部声明容器身份；含多个语义主题时，在各定义标题声明主代号并保留一处映射。编号被使用才登记，不能仅为占号新建计划；单纯文件路径/章节变化不分配新根。序号从 001 起，在相同前三段内递增，超过 999 可继续增长，不回收、不按排期重排。分配前只查该前缀已有行，不扫描全部源码/归档。
2. **先读码，后定位**：三段用于判断大致主题；需要精确条款时从入口直达根，再用完整子号/锚点定位。跨文档、聊天交接和审核结论使用完整码＋中文标题＋链接；上下文唯一的同根正文可短写。稳定引用需要锚点时只在被引用的定义处加，不全库生成锚点。
3. **身份稳定**：目录、负责人、状态、轮次变化不改码。范围实质改变到另一个主题时分配新码，旧入口标明取代/拆分并链接新定义，不能让旧码悄悄变义；小范围调整保留码并更新正文。语义段提示主责与主要合同，不是精确文件白名单，也不代表实施授权。
4. **归档不失联**：归档时更新既有入口的目标路径并保留原码；不把 Git 考古当日常定位入口。历史局部号仅作有来源的别名，不再赋予新含义；活跃显示迁移与机器兼容键分开，工具仍消费的字段保留原键并映射，不为文档改号改写工具判据。历史归档与旧门禁记录不批量重命名。ADR 等既有身份保留，新增引用说明对象类型，不重新分配一套同义号。
5. **避免重复权威**：同一规范只定义一次，其余以完整码链接或明确为派生检查项；编号相似不证明语义相同，编号不同也不授权重复定义。只有歧义妨碍当前追溯/验收时才要求本批消歧，不以旧格式存在为理由阻断无关业务批次。

<a id="work-identifiers"></a>

### 已分配工程代号（只定位，不复制状态）

下表直达OpenSpec唯一设计；Beads任务的spec_id使用同一路径。旧D1/R4C等映射仅在公共执行design的legacy-identifiers保留，原锚点仍可从[归档索引](../archive/INDEX-plans.md#beads-plan-migration)追溯。批准和当前状态只看 Beads 的具名记录，不从代号或 decision.closed 推断。

| 根代号 | 中文主题／主责范围 | 唯一定义 |
|---|---|---|
| P-RUN-X-001 | 统一计划容器与执行主干跨层合同；具体决策/批次按实际模块分配 | [gl-4tt设计](../../openspec/changes/runtime-foundations/design.md#record-gl-4tt) |
| P-RUN-IPC-001 | 产品执行编排的 App/Shizuku 进程通信合同；从属上项 | [RunHost typed wire 与状态机](../../openspec/specs/runhost-wire/spec.md#p-run-ipc-001) |
| E-REVIEW-RULE-001 | 设计审核与规划规则；旧 A–G 为本根条目组 | [设计与规划审查原则](design-review-principles.md#e-review-rule-001) |
| E-REVIEW-RULE-002 | 工程实施、批次与验证规范；章节号仅限本文 | [工程与设计规范](engineering-standards.md#e-review-rule-002) |
| E-REVIEW-RULE-003 | 设计原则的理论依据与解释；旧 D 为操作附录 | [设计哲学](design-philosophy.md#e-review-rule-003) |
| E-REVIEW-RULE-004 | Agent 执行纪律与门禁入口；编号不改变既有权威层级 | [AGENTS](../../AGENTS.md#e-review-rule-004) |
| E-DOC-RULE-001 | 文档、工程代号与计划模板；跨端架构问题表为附录 | [文档编写规范](documentation-standards.md#e-doc-rule-001) |
| E-DOC-UI-001 | 双 UI 套件的文档指南；范围为 UI，不属于某一运行批次 | [UI 生成指南](ui-ai-guide.md#e-doc-ui-001) |
| P-PAYLOAD-CORE-001 | Payload 必需性、失败与成功语义；有效正文在OpenSpec | [gl-kct设计](../../openspec/changes/runtime-foundations/design.md#record-gl-kct) |
| P-FINISH-CORE-001 | Finish 严重度与必执行语义；有效正文在OpenSpec | [gl-7c2设计](../../openspec/changes/runtime-foundations/design.md#record-gl-7c2) |
| P-PAYLOAD-IO-001 | Payload 接管确认与文件保留；有效正文在OpenSpec | [gl-2qz设计](../../openspec/changes/runtime-foundations/design.md#record-gl-2qz) |
| P-RUN-IO-001 | 运行输入 staging、脚本发布与产物持久化；有效正文在OpenSpec | [gl-dzp设计](../../openspec/changes/runtime-foundations/design.md#record-gl-dzp) |
| P-BACKEND-CORE-001 | BackendDefinition 组合根、BackendDescriptor 与具名BackendUi边界；BD-I3配置实现延后 | [gl-7yv设计](../../openspec/changes/runtime-foundations/design.md#record-gl-7yv) |
| P-UI-ICON-001 | Material/Miuix 全项目语义图标目录；有效正文在OpenSpec | [gl-rot设计](../../openspec/changes/semantic-icon-catalog/design.md#record-gl-rot) |
| P-UI-ICON-002 | Material/Miuix 当前依赖的全量图标目录需求；有效正文在OpenSpec | [gl-72l设计](../../openspec/changes/ui-followups/design.md#record-gl-72l) |
| P-UI-CORE-001 | App内KMP shared Compose UI、App/plugin共用唯一KMP `I18nRegistry`与catalog schema、Compose Multiplatform非文本资源、Android/Desktop平台壳、UI feature/package与单向presentation owner收口；远期目标已记录，exact design待plugin owner稳定后重开 | [gl-75t设计](../../openspec/changes/ui-followups/design.md#record-gl-75t) |
| P-BACKEND-X-001 | Backend 成员资格、删除与断开闭包；有效正文在OpenSpec | [gl-5r9设计](../../openspec/changes/runtime-foundations/design.md#record-gl-5r9) |
| P-BACKEND-X-002 | 多组件 PluginPackage、共享 API、独立输入 provider（含固定 Maven 版本的外部 Kernel Symbol Extractor consumer）、settings presentation与注册到唯一App/plugin `I18nRegistry`的authenticated bundle、thin variant 与 RunHost 安全装载 | [gl-sfg设计](../../openspec/changes/plugin-architecture/design.md#record-gl-sfg) |
| P-RUN-CORE-001 | 运行结果、会话终态与内部 core 收口；有效正文在OpenSpec | [gl-k6e设计](../../openspec/changes/runtime-foundations/design.md#record-gl-k6e) |
| E-DOC-X-002 | 开发环境、OpenSpec设计/Beads执行与CDA（旧接入沿革保留） | [OpenSpec/Beads职责](../../openspec/changes/planning-tool-responsibilities/design.md) |
| E-BUILD-X-001 | 当前构建闭环修复；精确设计在OpenSpec，原批准保持 | [gl-4on设计](../../openspec/changes/build-closure-repair/design.md#record-gl-4on) |
| E-BUILD-X-002 | Allure报告门薄适配及三棵提交候选的唯一hunk owner；原批准保持 | [Allure报告门设计](../../openspec/changes/allure-reporting-gates/design.md#record-allure-reporting-gates) |
| E-DOC-X-001 | 计划权威、归档与跨文档入口收口；有效正文在OpenSpec | [gl-8zk设计](../../openspec/changes/archive/2026-11-21-workflow-history/design.md#record-gl-8zk) |
| P-LOG-IO-001 | 日志自动导出与文件 sink；有效正文在OpenSpec | [gl-s7q设计](../../openspec/changes/logging-runtime/design.md#record-gl-s7q) |
| P-LOG-X-001 | 跨层单一日志中枢及结果归属边界；有效正文在OpenSpec | [gl-s7q设计](../../openspec/changes/logging-runtime/design.md#record-gl-s7q) |
| P-SETTINGS-IO-001 | 全项目持久化分类、完整 data class typed DataStore 与 run 前落盘边界；有效正文在OpenSpec | [gl-zgp设计](../../openspec/changes/datastore-runtime/design.md#record-gl-zgp) |
| P-DIAG-NATIVE-001 | Diagnose 的 native／JNA／shell 诊断纵向；有效正文在OpenSpec | [gl-yhz设计](../../openspec/changes/diagnose-handoff-foundations/design.md#record-gl-yhz) |
| P-RUN-HANDOFF-001 | common 固定位置脚本 handoff 与 Diagnose 单次 native chain；有效正文在OpenSpec | [gl-obq设计](../../openspec/changes/diagnose-handoff-foundations/design.md#record-gl-obq) |
| P-BACKEND-NATIVE-001 | 43284-Glued native carrier 候选；有效正文在OpenSpec | [gl-2u2设计](../../openspec/changes/plugin-followups/design.md#record-gl-2u2) |
| P-BACKEND-NATIVE-002 | 98260 native carrier 候选；有效正文在OpenSpec | [gl-2ot设计](../../openspec/changes/plugin-followups/design.md#record-gl-2ot) |

<a id="documentation-diagrams"></a>

## 画图

关键结构与流程**必须**画图。文字描述控制流要求读者在脑中重建拓扑；图直接给出拓扑，并让悬空的调用、
缺失的清理路径和矛盾的边界一眼可见（画图过程本身就是设计审查）。

- **什么时候必须画**（L 级改动）：新增/修改 route、跨层契约（Kotlin↔native）、会话与资源所有权、
  攻击阶段机、运行时状态与资源清理/失败恢复边界；这不要求准备源码 rollback artifact/fixture。
- **图种**：结构（类、持有关系）用类图或组件图；控制流用流程图/时序图；状态与出口用状态图；
  数据流用 flowchart。
- **用 Mermaid**：文本化，可进 git、可评审、可 diff、可随代码更新；图更新与代码同一批次。
- **一图一个关注点**（ISO/IEC/IEEE 42010 的 view/viewpoint 思路）：图给拓扑，正文给理由与例外，
  两者不互相抄。
- **全局 UML 至少维护两个时间视点**：
  - [当前状态全局 UML](full-process-uml.md#global-current-view)只根据当前工作树绘制已经存在的组件、调用、状态和 owner；尚未执行的门禁照实标为 `NOT_RUN`，不得把计划目标画成当前事实。
  - [活跃计划最终目标全局 UML](active-plan-target-uml.md#global-active-plan-target-view)合并所有当前有效活跃计划按依赖顺序完成后的目标；它只表达目标拓扑，不把设计认可、production 授权、实现或验证状态改写为 `PASS`。
- **按变化更新对应视点**：结构性源码改动同批更新当前图；活跃计划的目标、owner、合同或依赖顺序变化同批更新目标图；一批同时改变当前实现与最终目标时两份都更新。计划关闭或被取代后，重新按活跃计划集合计算目标图，不保留已失效目标作为当前 authority。
- **每个时间视点只有一处权威图**：当前图与目标图回答不同问题，不是冗余副本。其他文档链接对应视点；局部计划图只说明该批差异。确需第三份或更多全局 UML（例如迁移中间态或部署变体）时，文件头必须声明时间视点、问题、事实/计划来源以及与两份基础视图的关系，不建立无标注的平行 authority。
- 结构性改动必须更新适用视点中的 IPO/状态机/Class/Sequence；获准提交时，提交信息写明更新了哪一视点、哪张图。

<a id="documentation-bilingual"></a>

## 双语

`README.md` / `README_ZH.md` 以及已有 `*_ZH.md` 的对应文档（含 `docs/profile/**`）必须同批次同步，不允许单边漂移。
`docs/development/**` 用中文；代码注释与提交信息用英文。

<a id="documentation-templates"></a>

## 模板

**OpenSpec 设计内容（L 级改动必须，不另建旧式计划文件）**

以下八类内容写入同一OpenSpec proposal/design/specs的适用部分；小修按原分级裁剪，“目标与约束/进度”内区分本批承诺、后续需求和待裁定，具体依 [design-review-principles.md](design-review-principles.md) 的滚动细化与阶段规则。不新增独立需求台账，不改写已有专用任务清单。后续需求仅保留来源/目标、未知、owner 与重访触发，逐文件清单和详细验证矩阵只细化当前准备实施的批次；既有本批承诺移出仍须批准。

涉及 Kotlin/JNI/Native 跨层契约、资源 owner 或 Native transaction 时，可在下列通用八节内按需套用
[E-DOC-RULE-001/跨端附录：架构问题清单](kotlin-jni-native-architecture-template.md#cross-platform-appendix) 的问题清单。该文件是专用附录，
不得取代本节、另定计划目录或无条件引入 handle/common/Flow 等结构。

```markdown
# <根代号>：<中文主题> 计划（<YYYY-MM-DD>）
> 代号解释与定义入口：沿用工程代号规范；既有计划保留原题名，仅补头部声明。
## 现状与基线        # 分支、commit、当前行为、已知问题
## 目标与约束        # 用户结果/批准职责/本批缺口；承诺、后续、待裁定；引用最新有效裁定
## 改动清单          # 逐文件：文件 → 改动 → 理由
## 数据流/控制流差异  # 新旧对照；不变量说明
## 兼容性与失败恢复  # 源码恢复只引用 Git/Time Machine，不准备 rollback artifact/fixture
## 验证矩阵          # 本批行为/前提→方法/预期→对应成功层级；保留暂缓项及原恢复触发
## 明确保留          # 不动的部分及理由
## 进度              # 只用 Beads 原生状态/依赖；不是 design 内的 checkbox
```

**门禁记录（真机验证必须）**

```markdown
# <编号> 真机门禁：<主题>（<route>）— PASS/FAIL
对应提交 `<hash>`（`<commit message>`）
## 设备与入口   # 型号、kernel uname -r、入口、uid/selinux/seccomp、home
## 结果         # route_done 计数与 status/clean/step/errno、child is root、handoff、panic 与否
## 日志         # 文件名（与设备 Download/ghostlock-debug-log/<时间>/ 对应）
## 变更说明     # 本次验证针对的行为差异
```

历史门禁编号保留 S/CPP/U01/NS*/PROFILE-*；新增记录附所属完整根代号和验收主题，不为规范化重编号历史；**失败与通过同等归档**。失败记录写清影响（panic/重启/时长）、
根因与行动项，不追责——按事后剖析（postmortem）的字段写。

<a id="documentation-process"></a>

## 流程

- 文档与代码同一批次改；纯文档改动用 `docs:` 提交。
- 影响命令、字段、流程的改动必须回写文档。
- 历史文档归档至 `docs/archive/`，命名 `YYYYMMDD-HHMM-原名`；头部保留原路径、归档日期、归档原因、来源四要素并指向后继。恢复/门禁证据类更新 [INDEX-device-gates](../archive/INDEX-device-gates.md)，其余更新已有相应索引；`git show <commit>:<path>` 是只读兜底，**不得删历史**，不再以 `git rm` 代替现有归档约定。
- 行为已变化的文档加“现状注（日期）”，或按上面的方式归档。历史归档不批量改号；相关入口改链接而非复制正文，旧锚点与必要旧号映射保留。

## 检查清单

- [ ] 类型选对、读者明确
- [ ] 没有模糊结论；事实可追溯；数字可核对
- [ ] 模板字段完整（commit、日期、日志名）
- [ ] 双语同步；链接可用（相对路径）
- [ ] 归档已更新对应现有索引与受影响入口（不另建索引）

---

### 本轮查阅的公开实践及采用边界（2026-11-05）

| 来源 | 本项目采用什么 | 不额外引入什么 |
|---|---|---|
| [NASA WBS / Dictionary 术语](https://www.nasa.gov/ocfo/ppc-corner/ppc-glossary/)与[需求检查表](https://www.nasa.gov/reference/appendix-c-how-to-write-a-good-requirement/) | 按职责/成果分解、词典解释范围、唯一引用和追溯 | 不照搬航天项目管理规模；四段语法为项目自定 |
| [Jira 工作项 key](https://support.atlassian.com/jira-service-management-cloud/docs/work-with-requests-and-issues-in-jira-cloud-products/)与[修改 key 的影响](https://confluence.atlassian.com/adminjiraserver105/editing-a-project-key-1561363653.html) | 有意义的前缀＋序号；变更身份保留旧引用入口 | 不部署 Jira、不建数据库或内部 ID 双层系统 |
| [Google 审核标准](https://google.github.io/eng-practices/review/reviewer/standard.html)与[审核内容](https://google.github.io/eng-practices/review/reviewer/looking-for.html) | 事实优于偏好、非关键建议不阻断、避免投机性通用化；测试也要控制复杂度 | 不把实现评审要求搬成设计前必须已有全部测试；增量复审/token 规则是项目裁剪，不声称原文规定 |
| [Google 小变更](https://google.github.io/eng-practices/review/developer/small-cls.html) | 一批解决一个完整问题及必要测试闭包 | 不设置文档长度配额，不借并行做法越过项目阶段门 |
| [Scrum Guide 2020](https://scrumguides.org/scrum-guide.html#product-backlog)与[Agile 原则](https://agilemanifesto.org/principles.html) | 需求持续细化、选定工作与需求集合分开、减少无价值工作 | 不导入整套 Scrum 角色/会议，不省略安全/真机证据，不自称实施完整 Scrum |

禁止冗余副本/回滚演练是 AGENTS 中的项目裁定，不伪称以上资料普遍禁止备份或回滚。滚动细化、阶段相关阻断和增量复审继续以 [design-review-principles.md](design-review-principles.md) 为唯一权威，不在此重复流程。

来源：Google Developer Documentation Style Guide、Diátaxis、OASIS DITA（信息类型）、
Carroll Minimalism、UML（OMG / ISO/IEC 19505）、ISO/IEC/IEEE 42010 / 15289 / 2651x、
ADR/MADR、RFC 2119、GB/T 15834、Software Engineering at Google（Henderson, 2017）。
