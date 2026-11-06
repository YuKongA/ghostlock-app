# 文档编写规范（Documentation Standards）

<a id="e-doc-rule-001"></a>

> **代号**：E-DOC-RULE-001（工程支撑－文档－编写与编号规则）；跨端模板是本根附录，不另立规范。 定义入口见[工程代号入口](../plan/MASTER-PLAN.md#work-identifiers)。

> 本仓库一切文档的写法。风格参照 Google Developer Documentation Style Guide：短、直接、面向读者；
> 内容分类参照 Diátaxis。来源见文末。
> 效力与冲突处理见 [E-REVIEW-RULE-004/§1：最高思想与质疑义务](../../AGENTS.md#e-review-rule-004)；编号仅定位，不另设权威层级。
> 沿革：2026-11-03 用户裁定取消文档字节、页数与篇幅硬上限；“短、直接、一处权威”仍是质量要求，拆分只按职责、读者或权威边界。

## 先定类型

写之前回答两个问题：**给谁看？回答什么问题？** 然后在表里选一类，一篇文档只属于一类。

| 类型 | 写什么 | 放哪里 |
|---|---|---|
| 教程 / 快速开始 | 带人跑通一次 | `README.md`、`docs/profile/README.md` |
| 操作指南 | 完成一个任务 | `docs/development/*` |
| 参考 | 字段、命令、结构 | `docs/profile/PROFILE_SCHEMA.md`、`defaults.md`、`core/src/README.md` |
| 解释 | 为什么这样设计 | `design-philosophy.md`、`engineering-standards.md`、`docs/analysis/**`（历史） |
| 决策记录（ADR） | 一次架构决策：背景/决策/备选/后果 | `docs/analysis/adr/NNNN-*.md` |
| 计划 | L 级改动开始前 | `docs/analysis/*-plan.md`（完成后可归档） |
| 门禁记录 | 真机验证之后 | `docs/analysis/device-gates/`（归档，永久保留） |
| 发布说明 | 每个版本 | `docs/release-note-*.md`（归档） |

同一内容只写一处，别处链接；已有同类文档时更新它，不新建平行版本。

## 怎么写

- **短**。删掉不改变读者行为的句子；具体做法优先于抽象描述。
- **直接**。主动语态、现在时；写"运行 X"，不写"X 可以被运行"。
- **可核对**。数字、命令、日志名照实写；结论不超出证据。不写"应该没问题 / 大概 / 看起来正常"。
- **可追溯**。引用历史用 `git show <commit>:<path>`；计划与门禁记录必须有 commit 和日期。
- **不过度承诺**。不用"无缝、完美、彻底"这类词。
- 术语统一：`route`/`profile`/`gate`/`handoff`/`waiter`/`payload` 不译；中文用全角标点，
  代码与英文术语用反引号。
- 规范类文字用 必须/应当/可以（MUST/SHOULD/MAY）；其他文档不用命令语气。
- 英文文档按 Google style：active voice、second person、present tense、sentence case 标题。
- 链接用相对路径；跨文档引用给文件链接、完整代号与中文标题，必要时定位稳定锚点；不用裸章节号或裸局部字母号代替身份。

<a id="engineering-identifiers"></a>

## 工程代号：全局短码、局部复合码

代号用于快速识别主题并直达定义，不承载完整变更清单。下列语法是本项目对公开实践的裁剪，不是国际标准规定；不新增编号服务、TSV 注册表、校验器或审批岗位。

### 全局与局部

- **全局短码 `R<n>`**：仅指 [engineering-rules.md](engineering-rules.md) 的全工程规则，例 `R1：单一权威`；该表是 R 语义的唯一来源。全局表示适用范围，不表示最高权威：教材仍高于经验规则，教材条文继续引用具名章节，不为编号改写教材或另造“最高守则”。
- **局部根代号 `<类别>-<主模块>-<范围>-<序号>`**：如 `P-LOG-IO-001：日志文件输出`，前三段可反推产品/工程、主责模块和主要合同领域，末段区分同一作用域的不同工作。计划和局部规范均可有根代号；对象类型、状态、版本和负责人写在正文，不再塞进代号。
- **局部子项 `<根代号>/<类型><序号>`**：新增条目用 `/需求01`、`/决策01`、`/批次01`、`/问题01`，标题同时写中文含义；同一根的同类型连续分配，复审和另开报告不重置。问题分配先查该根的既有审核记录；在根文档的已有审核位置保留“问题号→定义锚点”链接，不另建问题台账。普通段落用 1、1.1，不给每段分配永久代号。
- **活跃旧号迁移为语义主代号**：旧 D1/R4C 等不能仅套统一计划前缀。按条目实际主责模块与合同领域分组，使用如 `P-PAYLOAD-CORE-001/决策01`、`P-LOG-X-001/批次01`；标题同时写中文主题。一个文件可定义多个语义根，不因此拆文件/新建计划；同一合同下连续决策/批次共用根，真正不同领域使用不同根。旧号只在原计划一处映射及原证据中保留，旧锚点继续可达；不重新批准、排序或重写历史。规范的旧 A–G 等仍按自身根定位，与产品决策不混用。

### 唯一短词表

词义按固定槽位读取，模块是稳定职责而非目录名。只有实际出现新职责且现有码确实不适用时才补一行；禁止每份计划自创缩写或预建未来模块。

| 槽位 | 代号 | 含义 |
|---|---|---|
| 工程类别 | P | 产品功能与其运行架构 |
| 工程类别 | E | 构建、工具、规则和文档等工程支撑 |
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
| 领域/范围 | UI | 用户界面与交互 |
| 领域/范围 | CORE | 模块内部模型与编排逻辑 |
| 领域/范围 | IO | 文件、流与持久化合同 |
| 领域/范围 | IPC | 进程间通信与 wire 合同 |
| 领域/范围 | NATIVE | native 构建、绑定与执行边界 |
| 领域/范围 | RULE | 工作规范与验收规则 |
| 领域/范围 | X | 由具名主模块负责的跨模块/跨层合同，正文必须列出边界，不代表“全仓任意修改” |

[P-LOG-IO-001：日志导出](../plan/backend-root-execution-unification-plan.md#p-log-io-001)、[P-LOG-X-001：跨层日志中枢](../plan/backend-root-execution-unification-plan.md#p-log-x-001)、[P-RUN-IPC-001：RunHost wire](../plan/backend-root-execution-runhost-wire.md#p-run-ipc-001) 均为已有内容的实际身份；`E-BUILD-NATIVE-001` 仍仅为未分配示例。`CORE` 指模块内部合同，`IO` 指文件/流，`X` 只用于确有具名 owner 的跨层/跨模块合同，不作为不分析归属的兜底；其它影响仍列在原范围中。测试随被验证合同归属，不因 test 字样自动成为工程工具任务。

### 分配、定位与兼容

1. **一处入口**：复用 [MASTER-PLAN.md](../plan/MASTER-PLAN.md#work-identifiers) 的代号入口：根代号、中文标题、定义链接。每个根只登记一次；文档头部声明容器身份；含多个语义主题时，在各定义标题声明主代号并保留一处映射。编号被使用才登记，不能仅为占号新建计划；单纯文件路径/章节变化不分配新根。序号从 001 起，在相同前三段内递增，超过 999 可继续增长，不回收、不按排期重排。分配前只查该前缀已有行，不扫描全部源码/归档。
2. **先读码，后定位**：三段用于判断大致主题；需要精确条款时从入口直达根，再用完整子号/锚点定位。跨文档、聊天交接和审核结论使用完整码＋中文标题＋链接；上下文唯一的同根正文可短写。稳定引用需要锚点时只在被引用的定义处加，不全库生成锚点。
3. **身份稳定**：目录、负责人、状态、轮次变化不改码。范围实质改变到另一个主题时分配新码，旧入口标明取代/拆分并链接新定义，不能让旧码悄悄变义；小范围调整保留码并更新正文。语义段提示主责与主要合同，不是精确文件白名单，也不代表实施授权。
4. **归档不失联**：归档时更新既有入口的目标路径并保留原码；不把 Git 考古当日常定位入口。历史局部号仅作有来源的别名，不再赋予新含义；活跃显示迁移与机器兼容键分开，工具仍消费的字段保留原键并映射，不为文档改号改写工具判据。历史归档与旧门禁记录不批量重命名。ADR 等既有身份保留，新增引用说明对象类型，不重新分配一套同义号。
5. **避免重复权威**：同一规范只定义一次，其余以完整码链接或明确为派生检查项；编号相似不证明语义相同，编号不同也不授权重复定义。只有歧义妨碍当前追溯/验收时才要求本批消歧，不以旧格式存在为理由阻断无关业务批次。

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
- **一个结构只有一处权威图**，其他文档链接它（沿用历史 `routes.md` "核心维护图唯一强制更新"的做法）。

<a id="documentation-bilingual"></a>

## 双语

`README.md` / `README_ZH.md` 与 `docs/profile/**` 的 `_ZH` 必须同批次修改，不允许单边漂移。
`docs/development/**` 用中文；代码注释与提交信息用英文。

<a id="documentation-templates"></a>

## 模板

**计划（L 级改动必须）**

沿用下列八节；“目标与约束/进度”内区分本批承诺、后续需求和待裁定，具体依 [design-review-principles.md](design-review-principles.md) 的滚动细化与阶段规则。不新增独立需求台账，不改写已有专用任务清单。后续需求仅保留来源/目标、未知、owner 与重访触发，逐文件清单和详细验证矩阵只细化当前准备实施的批次；既有本批承诺移出仍须批准。

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
## 进度              # [ ]/[x] 跟踪交付；关闭凭对应证据，不凭已改源码或已做检查
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
- 删文档 = 归档到 git 历史（`git rm`，不改写历史），并更新 `AGENTS.md` 的历史索引。
- 行为已变化的文档加"现状注（日期）"，或按上面的方式归档。

## 检查清单

- [ ] 类型选对、读者明确
- [ ] 没有模糊结论；事实可追溯；数字可核对
- [ ] 模板字段完整（commit、日期、日志名）
- [ ] 双语同步；链接可用（相对路径）
- [ ] 删除/归档已反映到 `AGENTS.md`

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
