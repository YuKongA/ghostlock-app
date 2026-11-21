# 已完成文档工具批与被纠正的分工沿革

## Context and scope

历史工具方案，已由planning-tool-responsibilities取代；不是当前执行指令。

来源：`043fdf6a:.beads/issues.jsonl`；保留语义代号和原锚点。本文中的历史进度/门结果文字仅是其具名输入的沿革，当前状态从proposal列出的Beads读取。旧机制由正文已注明的后继差异取代；基线不重新授权实施。

## Decisions

<a id="record-gl-8zk"></a>

## E-DOC-X-001：已完成旧权威收口沿革

<!-- migrated-source: 043fdf6a Beads gl-8zk description -->
2026-11-21迁入的共享设计记录，不是新的执行任务。closed仅表示该记录收录完毕，不表示业务实现、新批准或门禁通过。
原docs批完成沿革；本次Beads迁移见gl-ind，不重开旧72文件任务。

<!-- migrated-source: design gl-8zk -->
<!-- source: f882e670:docs/plan/backend-root-execution-unification-plan.md:L1068-L1091 -->
<a id="p-run-x-001-batches"></a>

#### 14. 实施批次与实时 checkbox

每批是可独立编译/验证的 caller closure；定义的 DELETE/MOVE 与全部 production caller 同批，禁止跨批 typealias/compat wrapper 维持假绿。上一批绿并记录后才进入下一批。同一时刻一条 writer；Gradle/native gate 串行。

<a id="e-doc-x-001"></a>

<a id="e-doc-x-001-batch-01"></a>

<a id="p-run-x-001-r0"></a>

##### E-DOC-X-001/批次01 设计与 authority 收口（当前批；docs only）

- [x] 需求基线、JNA 决策、current-tree inventory。
- [x] 推荐语义、owner、wire/state ledger、file ledger、lifecycle、批次、验证候选。
- [x] design-critic 阻断归零（第六轮冻结候选总判：可进入用户批准 gate）。
- [x] 用户批准 D1-D9 与 R1（2026-11-04：回复“批准”）。
- [x] 按 [`backend-root-execution-plan-disposition.tsv`](/docs/archive/20261121-1301-backend-root-execution-plan-disposition.tsv) 归档/标注 72 件被取代 authority，更新三个 pointer 与唯一计划索引；production source 零改动。

<a id="p-backend-x-001-batch-01"></a>

<a id="p-run-x-001-r1"></a>



<a id="acceptance-gl-8zk"></a>

### 验收定义与原边界：gl-8zk

<!-- migrated-source: acceptance_criteria gl-8zk -->
保留原文各行为判据、当轮证据和未关闭设备门。此decision收录完成不关闭任何关联业务任务。

批准/审核/实际结果：Beads `gl-8zk` notes；本段不维护实时勾选或新PASS。

<a id="record-gl-1w2"></a>

## E-DOC-X-002：开发环境与 OpenSpec/Beads/CDA 接入

<!-- migrated-source: 043fdf6a Beads gl-1w2 description -->
历史已完成交付；OpenSpec接入方法已由gl-ind取代，以下原交付与证据不作为当前操作指令。
用户已授权目标见 openspec/changes/engineering-workflow/proposal.md；只流程/文档/配置/测试，保留原批准和门禁。

<!-- migrated-source: design gl-1w2 -->
历史已完成接入记录；其中OpenSpec分工已由gl-ind的Beads-only迁移取代，不是当前操作指令。

#### Context and scope

EVOLUTION。用户已选择工具组合并授权安装/接入/试点/活跃计划迁移；原产品计划批准不被该授权扩大。范围限流程、文档、配置和相关测试。固定 OpenSpec2.1.1、Beads1.3.4；CDA 来源 commit `04d2e998f19b08be6ba7f697315f3a16ecdf2708`。

VERIFIED：BXP3-B1 批准与 P0 已关闭，I/V1/V2/V3 尚未关闭；当前148行清单位于原计划 §19.5.70，不是旧240行 TSV。旧未勾选项包含被取代设计；不能自动全部导入。开发环境实际接线问题见[环境文档](/docs/development/development-environment.md)。

#### Decisions

1. **一类信息一处维护**：工程规则保留 docs/development；设计变更由 OpenSpec 定位。既有计划全文保留原处，迁移 change 仅链接当前有效章节、批准和验收；新设计在 change 中编写，不再另写平行长计划。Beads 只维护可执行任务/候选、依赖、认领、运行证据和关闭状态；不得把描述摘要当第二份批准。原计划进度和 MASTER/ROADMAP 改为直达链接，旧证据明确为迁移前快照。
2. **用原生能力而非同步系统**：项目 schema 保留 proposal/specs/design，`apply.tracks: null` 删除 Markdown tasks authority。无产品合同变化使用上游 `skip_specs`；只对真正行为变更维护 delta specs。Beads `spec_id`/`external_ref` 指向设计与原语义编号，生成 ID 不替代人类语义码。
3. **保存与协作**：Beads 使用嵌入式 Dolt，无常驻服务器。CLI 是唯一写入入口；`.beads/issues.jsonl` 是原生命令生成的可携带任务投影，禁止手编，不是第二个任务表或完整数据库备份。`bootstrap` 支持 fresh clone 导入；跨机器并发同步使用原生 Dolt remote，须获准发布后再启用，不用 JSONL 双向合并。关闭工具不经范围核对的自动 push、Git stage、backup 和 hooks；Dolt 内部事务历史不等于源码 Git commit。首次 init 自动创建本地 Git 提交 `cebb1804`，已向用户报告；用户于2026-11-21纠正旧提交限制，完成并验证本批后由 Agent 主动精确提交，不再等用户手动催促，不擅自重写历史。后续用 bootstrap，不重复 init。
4. **任务状态不是批准或 PASS**：`ready` 只说明依赖无阻塞。待设计/未批准候选使用 deferred，缺具名条件的任务 blocked；close 必须说明完成/取代/取消及对应证据，不把取消写 PASS。迁移只导入有效剩余工作，不重开已完成批次；未来需求不阻断无关当前批次。
5. **保留原职责**：CDA 只用于实质技术设计，结论写原 design；作者自检仍修原交付，critic 仍非作者只读。工具 schema/格式检查属机械核对。原批准与真实设备门不由 artifact exists、all_done、Beads close 或工具升级替代。

#### Verification and authorization

- 先用现有“按任务配置开发环境”流程变更跑通：创建真实交付任务和验收任务 → 依赖阻止提前 ready/close → claim → 完成文档与链接/版本检查 → close → 后继 ready。保存证据到原任务，不建测试报告。
- 检查 schema/apply 不生成 tasks.md；修改 Beads 状态不要求更新 OpenSpec 进度。重新打开 CLI 核对状态持久化；检查导出的依赖、证据、原语义码和 NOT_RUN 没有丢失。
- 试点通过后迁移活跃批次，批准正文、当前148行 scope、验收矩阵不改。仅一次核对取代关系、任务依赖、未批准事项不可执行及原门保持 NOT_RUN；其它聊天仍在实施 B1，落笔前重读对应状态，不替实施者认领或写生产代码。
- 本轮不运行产品构建/AVD/真机门；这些均 NOT_RUN。安装成功不证明工程配置或产品行为通过。

作者自检：已核真实使用者（作者、执行 agent、新 clone）、工具实际 CLI、状态与设计边界及初始迁移清单。

独立审查（2026-11-21，`/root/workflow_critic`）：设计阻断0，可进入既定接入与试点。Lead 核对实际 schema/skill 切换、JSONL 最后更新与活跃实施者的状态交接，不再追加 critic PASS；本结论不证明试点或产品门通过。原实施聊天 `01a1ec39-4b71-7911-a080-05c1b7ae5fa7` 当前为 idle，最新用户优先项为构建闭环修复，精确设计待批准；迁移必须保留这一更新，不继续把 B1 实施描述为唯一立即执行项。


<a id="acceptance-gl-1w2"></a>

### 验收定义与原边界：gl-1w2

<!-- migrated-source: acceptance_criteria gl-1w2 -->
环境指南完成；固定版本与skill路由生效；流程试点通过后迁移有效活跃任务；无第二进度authority。

批准/审核/实际结果：Beads `gl-1w2` notes；本段不维护实时勾选或新PASS。

<a id="record-gl-1w2.1"></a>

## E-DOC-X-002/批次01：开发环境指南与按任务入口

<!-- migrated-source: 043fdf6a Beads gl-1w2.1 description -->
历史已完成交付；OpenSpec接入方法已由gl-ind取代，以下原交付与证据不作为当前操作指令。
真实交付：docs/development/development-environment.md、README双语入口、AGENTS按任务配置；官方安装来源和当前接线问题分开。

<!-- migrated-source: design gl-1w2.1 -->
历史已完成接入记录；其中OpenSpec分工已由gl-ind的Beads-only迁移取代，不是当前操作指令。

#### Context and scope

沿用[接入设计](/docs/archive/20261121-1301-openspec/changes/engineering-workflow/design.md)及其作者自检、独立 critic 阻断0结论，不另开审查。唯一操作文档为[开发环境](/docs/development/development-environment.md)，固定版本来源保留在原构建与包配置中。

#### Decisions

按任务分开文档/规划、Android、host native、Rust 与设备依赖。JDK25 daemon 与 JDK21 compiler 分开；单测真实 builtin plugin 依赖 NDK；旧源码路径/硬编码平台是仓库接线问题，不通过安装更多工具掩盖。

#### Verification and authorization

文档链接、版本源及实际 CLI 验证完成后关闭 gl-1w2.1；gl-1w2.2 以真实依赖的拒绝提前关闭、ready 转移、claim、导出重读与原生归档验证证明流程接入。运行证据仅写 Beads；本变更不运行产品构建、AVD 或真机门，这些保持 NOT_RUN。


<a id="acceptance-gl-1w2.1"></a>

### 验收定义与原边界：gl-1w2.1

<!-- migrated-source: acceptance_criteria gl-1w2.1 -->
本地链接存在；固定版本与Gradle/npm实际配置吻合；无工具空缺误报为产品失败；产品构建/AVD/真机 NOT_RUN。

批准/审核/实际结果：Beads `gl-1w2.1` notes；本段不维护实时勾选或新PASS。

<a id="record-gl-1w2.2"></a>

## E-DOC-X-002/批次01：验证单一任务状态与可携带投影

<!-- migrated-source: 043fdf6a Beads gl-1w2.2 description -->
历史已完成交付；OpenSpec接入方法已由gl-ind取代，以下原交付与证据不作为当前操作指令。
对前项真实文档交付执行Beads依赖/claim/close、OpenSpec无tasks、导出重读及原生archive验证；不新增检查流程或产品门。

<!-- migrated-source: design gl-1w2.2 -->
设计唯一位置（仅按当前任务疑点展开，不复读所有依赖）：
- Beads [gl-1w2](design.md#record-gl-1w2)：`npm run --silent bd -- show gl-1w2 --json --brief-deps`

<a id="acceptance-gl-1w2.2"></a>

### 验收定义与原边界：gl-1w2.2

<!-- migrated-source: acceptance_criteria gl-1w2.2 -->
依赖未满足时不ready且close拒绝；交付关闭后本项ready；状态与依赖/证据持久化；OpenSpec不生成tasks.md；产品门NOT_RUN。

批准/审核/实际结果：Beads `gl-1w2.2` notes；本段不维护实时勾选或新PASS。

<a id="record-gl-1w2.3"></a>

## E-DOC-X-002/批次02：迁移有效活跃计划状态

<!-- migrated-source: 043fdf6a Beads gl-1w2.3 description -->
历史已完成交付；OpenSpec接入方法已由gl-ind取代，以下原交付与证据不作为当前操作指令。
试点已通过；按当前有效scope/批准/未运行门迁移，旧正文/历史映射保留，停止重复checkbox与状态摘要；对已idle实施聊天做用户授权的一次入口交接。

<!-- migrated-source: design gl-1w2.3 -->
设计唯一位置（仅按当前任务疑点展开，不复读所有依赖）：
- Beads [gl-1w2](design.md#record-gl-1w2)：`npm run --silent bd -- show gl-1w2 --json --brief-deps`

<a id="acceptance-gl-1w2.3"></a>

### 验收定义与原边界：gl-1w2.3

<!-- migrated-source: acceptance_criteria gl-1w2.3 -->
所有有效剩余交付和deferred需求可从OpenSpec/Beads定位；未批准不ready实施；原批准及148行scope不变；旧240/51行不复活；历史归档不改；不改生产代码。

批准/审核/实际结果：Beads `gl-1w2.3` notes；本段不维护实时勾选或新PASS。

<a id="record-gl-ind"></a>

## E-DOC-X-002/批次03：Beads 设计与执行单一入口、退役旧计划系统

<!-- migrated-source: 043fdf6a Beads gl-ind description -->
2026-11-21 用户明确要求：有效设计、范围、批准记录和验收判据完整迁入 Beads；旧计划系统归档，OpenSpec 退出流程；接好 AGENTS 与相关 skill。保留现行规范、公共合同、独立审查、真机门禁及原批准状态；不改生产代码、不新增平行流程，验证后主动提交。

<!-- migrated-source: design gl-ind -->
迁移按既有语义主题/批次，不按段落或TSV行创建任务。现有31条任务身份、状态、依赖、批准保持；27项活跃任务补齐原文设计与矩阵；跨任务共同设计用原生 decision 记录集中承载，relates-to 仅导航不设新阻断。decision.closed 只说明记录已收录，不表示所属业务实现或批准；批准原文、日期、范围与取代裁定必须在正文就近保留。保留产品规范/public wire为现行文档，代号注册表移入现有文档规范。原计划/路标/清单及OpenSpec工件移入既有archive并保留旧号、锚点、原来源；新任务日常操作不回读归档。只改文档、skills、Beads及npm工具配置。验证非空长设计/批准/依赖经原生export/bootstrap逐字段保持、原任务状态不变、现行入口不再要求旧计划/OpenSpec，检查链接与skill。生产构建/AVD/真机全部NOT_RUN。

<a id="acceptance-gl-ind"></a>

### 验收定义与原边界：gl-ind

<!-- migrated-source: acceptance_criteria gl-ind -->
有效设计/范围/批准与验收完整可从Beads读取，实际共享内容只定义一次；旧系统仅历史；31条原有任务状态、原真实阻断边和未执行证据不变；规范/public合同及作者自检/独立审查/真机门禁保持；仅本批范围本地commit，生产改动不夹带。

批准/审核/实际结果：Beads `gl-ind` notes；本段不维护实时勾选或新PASS。
