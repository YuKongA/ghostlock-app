# MASTER-PLAN：GhostLock 设计与任务入口

> 2026-11-04 用户已批准 Backend root execution D1–D9 与 R0→R1 顺序。当前唯一实施 authority 是 [backend-root-execution-unification-plan.md](backend-root-execution-unification-plan.md)。历史计划已按 [backend-root-execution-plan-disposition.tsv](backend-root-execution-plan-disposition.tsv) 归档，不再从本文件重复其实施指令。

<a id="work-identifiers"></a>

## 代号入口（只定位，不重复进度）

词义与分配见[工程代号规范](../development/documentation-standards.md#engineering-identifiers)；全局 `R<n>` 直达[R1–R43：全局工程规则表](../development/engineering-rules.md#global-engineering-rules)，不指本计划批次。入口保留已分配码及归档/取代指针，不另建注册文件。

| 根代号 | 中文主题／主责范围 | 唯一定义 |
|---|---|---|
| P-RUN-X-001 | 统一计划容器与执行主干跨层合同；具体决策/批次按实际模块分配 | [Backend root execution 统一计划](backend-root-execution-unification-plan.md#p-run-x-001) |
| P-RUN-IPC-001 | 产品执行编排的 App/Shizuku 进程通信合同；从属上项 | [RunHost typed wire 与状态机](backend-root-execution-runhost-wire.md#p-run-ipc-001) |
| E-REVIEW-RULE-001 | 设计审核与规划规则；旧 A–G 为本根条目组 | [设计与规划审查原则](../development/design-review-principles.md#e-review-rule-001) |
| E-REVIEW-RULE-002 | 工程实施、批次与验证规范；章节号仅限本文 | [工程与设计规范](../development/engineering-standards.md#e-review-rule-002) |
| E-REVIEW-RULE-003 | 设计原则的理论依据与解释；旧 D 为操作附录 | [设计哲学](../development/design-philosophy.md#e-review-rule-003) |
| E-REVIEW-RULE-004 | Agent 执行纪律与门禁入口；编号不改变既有权威层级 | [AGENTS](../../AGENTS.md#e-review-rule-004) |
| E-DOC-RULE-001 | 文档、工程代号与计划模板；跨端架构问题表为附录 | [文档编写规范](../development/documentation-standards.md#e-doc-rule-001) |
| E-DOC-UI-001 | 双 UI 套件的文档指南；范围为 UI，不属于某一运行批次 | [UI 生成指南](../development/ui-ai-guide.md#e-doc-ui-001) |
| P-PAYLOAD-CORE-001 | Payload 必需性、失败与成功语义；定义仍在统一计划内 | [Payload 必需性、失败与成功语义](backend-root-execution-unification-plan.md#p-payload-core-001) |
| P-FINISH-CORE-001 | Finish 严重度与必执行语义；定义仍在统一计划内 | [Finish 严重度与必执行语义](backend-root-execution-unification-plan.md#p-finish-core-001) |
| P-PAYLOAD-IO-001 | Payload 接管确认与文件保留；定义仍在统一计划内 | [Payload 接管确认与文件保留](backend-root-execution-unification-plan.md#p-payload-io-001) |
| P-RUN-IO-001 | 运行输入 staging、脚本发布与产物持久化；定义仍在统一计划内 | [运行输入 staging、脚本发布与产物持久化](backend-root-execution-unification-plan.md#p-run-io-001) |
| P-BACKEND-CORE-001 | BackendDefinition 组合根、BackendDescriptor 与具名BackendUi边界；BD-I3配置实现延后 | [Backend 组合与UI边界合同](backend-root-execution-unification-plan.md#p-backend-core-001) |
| P-UI-ICON-001 | Material/Miuix 全项目语义图标目录；定义仍在统一计划内 | [统一语义图标目录](backend-root-execution-unification-plan.md#p-ui-icon-001) |
| P-UI-ICON-002 | Material/Miuix 当前依赖的全量图标目录需求；定义仍在统一计划内 | [全量图标目录需求](backend-root-execution-unification-plan.md#p-ui-icon-002) |
| P-UI-CORE-001 | App内KMP shared Compose UI、App/plugin共用唯一KMP `I18nRegistry`与catalog schema、Compose Multiplatform非文本资源、Android/Desktop平台壳、UI feature/package与单向presentation owner收口；远期目标已记录，exact design待plugin owner稳定后重开 | [App KMP shared UI、i18n与feature/package收口](backend-root-execution-unification-plan.md#p-ui-core-001) |
| P-BACKEND-X-001 | Backend 成员资格、删除与断开闭包；定义仍在统一计划内 | [Backend 成员资格、删除与断开闭包](backend-root-execution-unification-plan.md#p-backend-x-001) |
| P-BACKEND-X-002 | 多组件 PluginPackage、共享 API、独立输入 provider（含固定 Maven 版本的外部 Kernel Symbol Extractor consumer）、settings presentation与注册到唯一App/plugin `I18nRegistry`的authenticated bundle、thin variant 与 RunHost 安全装载 | [Backend 插件分发、输入提供与多架构演进](backend-root-execution-unification-plan.md#p-backend-x-002) |
| P-RUN-CORE-001 | 运行结果、会话终态与内部 core 收口；定义仍在统一计划内 | [运行结果、会话终态与内部 core 收口](backend-root-execution-unification-plan.md#p-run-core-001) |
| E-DOC-X-002 | 开发环境与 OpenSpec/Beads/CDA 接入 | [接入设计](../../openspec/changes/engineering-workflow/design.md) |
| E-BUILD-X-001 | 当前构建闭环修复；精确设计与批准沿原聊天 | [构建修复入口](../../openspec/changes/build-closure-repair/design.md) |
| E-DOC-X-001 | 计划权威、归档与跨文档入口收口；定义仍在统一计划内 | [计划权威、归档与跨文档入口收口](backend-root-execution-unification-plan.md#e-doc-x-001) |
| P-LOG-IO-001 | 日志自动导出与文件 sink；定义仍在统一计划内 | [日志自动导出与文件 sink](backend-root-execution-unification-plan.md#p-log-io-001) |
| P-LOG-X-001 | 跨层单一日志中枢及结果归属边界；定义仍在统一计划内 | [跨层单一日志中枢及结果归属边界](backend-root-execution-unification-plan.md#p-log-x-001) |
| P-SETTINGS-IO-001 | 全项目持久化分类、完整 data class typed DataStore 与 run 前落盘边界；定义仍在统一计划内 | [typed DataStore 持久化收口](backend-root-execution-unification-plan.md#p-settings-io-001) |
| P-DIAG-NATIVE-001 | Diagnose 的 native／JNA／shell 诊断纵向；定义仍在统一计划内 | [Diagnose 的 native／JNA／shell 诊断纵向](backend-root-execution-unification-plan.md#p-diag-native-001) |
| P-RUN-HANDOFF-001 | common 固定位置脚本 handoff 与 Diagnose 单次 native chain；定义仍在统一计划内 | [common 固定位置脚本 handoff](backend-root-execution-unification-plan.md#p-run-handoff-001) |
| P-BACKEND-NATIVE-001 | 43284-Glued native carrier 候选；定义仍在统一计划内 | [43284-Glued native carrier 候选](backend-root-execution-unification-plan.md#p-backend-native-001) |
| P-BACKEND-NATIVE-002 | 98260 native carrier 候选；定义仍在统一计划内 | [98260 native carrier 候选](backend-root-execution-unification-plan.md#p-backend-native-002) |

活跃条目的[旧号→语义主代号映射](backend-root-execution-unification-plan.md#plan-semantic-identifiers)只维护在原计划；新标题、排期、剩余项和文档 ledger 均采用模块/领域码。一个文件可容纳多个语义主题，不按文件拆新计划；根号不表示批准。原条目状态、历史顺序及取代关系不由编号推导。

其它旧号仍按来源区分：[critic D1：验证覆盖](../development/design-review-principles.md#review-rule-d1)、[跨端模板问题组](../development/kotlin-jni-native-architecture-template.md#cross-platform-appendix)、[历史组件 R1/R8](../development/adding-a-component.md#legacy-component-identifiers)、[AGENTS 旧 R9 指针表](../../AGENTS.md#agents-pointers)、[历史 requirements F/I/Q/U](../development/requirements.md#legacy-requirement-identifiers)。[已终止 43499 child](43499-glued-product-carrier-plan.md)的 D1/S1 仍是原附件局部号，不能映射成主计划决策。

历史 finding 继续按具名轮次定位：[R4A 首轮](backend-root-execution-unification-plan.md#p-run-x-001-r4a-review)、[R4B 首轮](backend-root-execution-unification-plan.md#p-run-x-001-r4b-review)、[R4C 记录](backend-root-execution-unification-plan.md#p-run-x-001-r4c-review)、[R6 沿革](backend-root-execution-unification-plan.md#p-run-x-001-r6-review)。这些标题是历史来源，不是当前主代号。

## 当前 authority

- [P-RUN-X-001：产品目标、边界、决策、批次与门禁](backend-root-execution-unification-plan.md#p-run-x-001)
- [P-RUN-IPC-001：RunHost wire、状态机、GLR1 与产品成功投影](backend-root-execution-runhost-wire.md#p-run-ipc-001)
- 当前 BXP3-B1 scope：[决策03/批次01 §19.5.70 的148行](backend-root-execution-unification-plan.md#p-backend-x-002-decision-03-batch-01)；旧[TSV](backend-root-execution-unification-ledger.tsv)仅保留既往批次记录，不是当前240行实施入口。
- 活跃计划 disposition：[backend-root-execution-plan-disposition.tsv](backend-root-execution-plan-disposition.tsv)
- 历史索引：[INDEX-plans.md](../archive/INDEX-plans.md)

<a id="active-task-routing"></a>

## 当前任务入口（2026-11-21 切换）

**进度唯一写入 Beads，设计/批准保持原锚点。** `npm run --silent bd -- list` 查看任务；`ready --json` 只表示依赖可用，不代表实施获准。不得继续同步 MASTER、ROADMAP 或旧计划 checkbox。新 clone 先按[环境文档](../development/development-environment.md#planning-tools) bootstrap。

| 主题 | OpenSpec / 原设计入口 | Beads 定位 |
|---|---|---|
| 环境与工具接入 | [接入](../../openspec/changes/engineering-workflow/design.md) | `gl-1w2` |
| 运行/日志未关闭证据与具名技术债 | [runtime-evidence](../../openspec/changes/runtime-evidence/proposal.md) | `gl-uat`、`gl-32v`、`gl-f47`、`gl-68z`、`gl-51h`、`gl-v2i`、`gl-mdl`、`gl-ftu`、`gl-8dj`、`gl-dcu` |
| P-BACKEND-X-002：按真实 consumer 触发的后续事项 | [plugin-followups](../../openspec/changes/plugin-followups/proposal.md) | `gl-2zi`、`gl-9lz`、`gl-tz3`、`gl-r8h`、`gl-dt8`、`gl-2u2`、`gl-2ot`、`gl-m0f` |
| P-UI-CORE-001 与 P-UI-ICON-002：各自后续设计 | [ui-followups](../../openspec/changes/ui-followups/proposal.md) | `gl-75t`、`gl-72l` |
| P-DIAG-NATIVE-001/批次02：真实 consumer 压力设计 | [diagnose-stress-design](../../openspec/changes/diagnose-stress-design/proposal.md) | `gl-cdy` |
| P-SETTINGS-IO-001/批次01：Android runtime 验证 | [datastore-runtime](../../openspec/changes/datastore-runtime/proposal.md) | `gl-zgp` |
| P-BACKEND-X-002/决策03/批次01：builtin Diagnose plugin | [builtin-diagnose-plugin](../../openspec/changes/builtin-diagnose-plugin/proposal.md) | `gl-dek`、`gl-3ki`、`gl-bvj`、`gl-xme` |
| E-BUILD-X-001：当前构建闭环修复 | [build-closure-repair](../../openspec/changes/build-closure-repair/proposal.md) | `gl-4on` |

原语义代号→Beads的具名映射在各 change 的 proposal，仅作入口，不复制状态。旧240行插件scope、51行Diagnose scope、已结束lifecycle批与独立extractor库任务均不作为当前待办重新导入。

## 固定边界

- `raw uid 0` 只证明 backend primitive，不直接等于产品成功。
- Flow 保留为宿主内部 Kotlin operation orchestration／shared adapter，不进入公开插件 ABI；BXP3-B1的plugin入口只实现`:plugin:api` Java `DiagnosticBackendEntryPoint`，后继真实backend按其entry contract增量加接口，不恢复same-APK `BackendDefinition`权威。
- Payload 与 Finish 独立于 backend，并由唯一落盘 root script 承载。
- Finish、Payload 与 backend cleanup 各自保留 owner；不建立通用 executor façade。
- 产品成功唯一投影为 `RunCapability.PRODUCT_ROOT + RunTerminal.SUCCESS`。
- 任何 backend 通过目标设备门之前都不标 Ready/supported。
