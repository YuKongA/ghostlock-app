# MASTER-PLAN：GhostLock 主计划（唯一进度入口）

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
| P-UI-CORE-001 | UI feature/package、dead component 与单向 presentation owner 收口 | [UI feature/package 收口](backend-root-execution-unification-plan.md#p-ui-core-001) |
| P-BACKEND-X-001 | Backend 成员资格、删除与断开闭包；定义仍在统一计划内 | [Backend 成员资格、删除与断开闭包](backend-root-execution-unification-plan.md#p-backend-x-001) |
| P-RUN-CORE-001 | 运行结果、会话终态与内部 core 收口；定义仍在统一计划内 | [运行结果、会话终态与内部 core 收口](backend-root-execution-unification-plan.md#p-run-core-001) |
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
- 逐文件 implementation scope：[backend-root-execution-unification-ledger.tsv](backend-root-execution-unification-ledger.tsv)
- 活跃计划 disposition：[backend-root-execution-plan-disposition.tsv](backend-root-execution-plan-disposition.tsv)
- 历史索引：[INDEX-plans.md](../archive/INDEX-plans.md)

## 当前进度

- P-UI-ICON-001/批次01（开发者验收完成）：唯一人工项目 `Icons` 目录已落地88个current pair，current 30个direct upstream icon callsite与175条import已收口，目录外architecture命中0；debug/release独立assemble、114项debug unit、androidTest compile与31项host native均通过。2026-11-08开发者目视Material3/Miuix无显示错误并关闭IC-V1；随后裁定`verifyJnaDistribution`为不必要检查并从current task graph删除。P-UI-ICON-002另行登记全量图标目录需求。
- P-BACKEND-CORE-001/决策01-02+批次01（完成）：outer `BackendDefinition(descriptor, runtime, ui)`、纯 `BackendDescriptor`、唯一registry的runtime/UI投影、具名 `BackendUi` 及Custom占位已落地；旧data/domain catalog链、carrier wrapper与generic schema→Compose配置DSL已清零，BD-I3保持延后。architecture、unit、host native、双variant assemble与6.6 AVD UI通过；删除不必要的JNA distribution task后，clean及只构建debug/release的`nativeBuildAndroid`均通过，BD-I1/I2/V1关闭。
- P-LOG-X-001/批次03（完成）：46行caller闭包内已落地统一 `LogConnector` source contract及Kotlin、RunHost、FD adapter，LogHub只接draft。每source FIFO、raw acceptance全局commit、transport-private sequence、timestamp仅元数据、动态attach/detach与stop→drain→close顺序均由unit/architecture冻结；debug/release各120项unit、host 31项、双variant build、>10 MiB MediaStore/两皮肤UI 7项及App/Shizuku exact connector路径均通过。Incomplete只保留accepted prefix并追加普通Warn，不改变typed产品结果。
- P-SETTINGS-IO-001/批次01（source实现完成，Android exact runtime待补）：`UiPreferences`、`OperationalPreferences`、`AppSelection`、`PayloadDefinition`与`FinishDefinition`五个完整Kotlin data class已分别接入typed DataStore；旧SharedPreferences adapter、Payload/Finish v3/JCS内部codec/document authority均已清零。主进程幂等初始化、accepted transaction、Payload-only preview exact handle与run cutoff已接通；debug/release JVM unit各130项、architecture、host native 31项与双variant build通过。当前无获准AVD，Android instrumentation runtime保持`NOT_RUN`，因此DS-I1/I2/V2关闭而DS-V1保持开放。
- P-DIAG-NATIVE-001/批次02（producer/lifecycle候选保留，snapshot切片重开）：native opaque group、A→B churn、>10 MiB、drain后handoff等v4需求继续保留；旧通用GLK schema/repository/JCS `PROFILE` freeze及51行ledger不再授权实施。P-SETTINGS没有预建无consumer的Diagnose store；P-DIAG未来在真实stress UI/Flow consumer同批拥有Diagnose具名typed DataStore、per-run immutable snapshot、跨进程承载与Flow消费，须重新细化、critic复审并获用户批准，production仍`NOT_RUN`。
- P-RUN-X-001/批次02（完成）：RunHost纯合同已归`core/runhost`，client/wire/host adapter已归`data/runhost`，通用FD connector已归`data/log`，App framework entry已归`runhost/AppRunHostService`，`AndroidRunSessionAdapter`和`RunAdmissionPolicy`也已归位；旧owner/类名与caller-zero carrier清零。architecture、debug/release各128项unit、androidTest compile、native host 31/31、双variant build、AIDL zero-diff与6.6 AVD App/Shizuku exact smoke均通过。后续core/data、Host去重、backend与UI package收口仍未获实施授权。
- 当前执行顺序固定为：已完成的Icons/Backend/Log/DataStore普通门 → P-RUN-X-001/批次02 RunHost owner收口 → core/data与run coordinator边界 → shared Host去重 → backend package → P-UI-CORE-001（含root/composition↔UI service-locator收口）→ 重新细化P-DIAG-NATIVE-001/批次02 → lifecycle exact AVD → automatic export真实设备/最终验收。P-SETTINGS Android exact runtime、P-UI-ICON-002与真实carrier门保持原欠账/触发，不冒充完成。
- E-DOC-X-001/批次01/P-BACKEND-X-001/批次01：已完成 authority 收口与 disabled/original closure 删除，并保留当批通过记录。
- P-RUN-IO-001/批次01：Payload/Finish/root-script source implementation 已落地；host fault matrix=`31/31`、6.6 AVD ordinary-file staging/identity=`3/3`且均exit 0，真实carrier设备门保持 NOT_RUN。
- P-RUN-X-001/批次01：current source已由Backend批迁移到 `BackendDescriptor`/`BackendRuntime`/`BackendUi` 分层，typed RunHost、App/Shizuku clients、RunReport 与 UI implementation 已落地；正常transport/runtime继续PASS。lifecycle/cancel专属exact AVD与真实carrier设备门仍为NOT_RUN；本轮host-death connector路径通过不替代其余专属欠账。
- P-LOG-IO-001/批次02、P-LOG-X-001/批次01-03：全流式raw/processed、统一LogConnector source、UI/private/MediaStore独立unlimited sink queue已经落地，>10 MiB、source顺序与current 6.6 AVD证据已PASS；`RunReport`摘要是普通Info行，typed report仍为产品authority。automatic export真实设备门与最终验收仍为NOT_RUN。
- P-BACKEND-X-001/批次02：所有真攻击 backend 已从 Kotlin registry、Gradle开关/任务与APK native target graph断开，旧native实现保持source-only且不修改。
- P-DIAG-NATIVE-001/批次01与P-RUN-HANDOFF-001/批次01：release-visible DIAGNOSE、constructor-loaded JNA、真实DSO、SequentialFlow、native fixed-home handoff、LogHub及4/16 KiB App/direct-JNA/Shizuku短链均已完成；它始终为 `DIAGNOSTIC_ONLY`，不代表root产品成功。

## 固定边界

- `raw uid 0` 只证明 backend primitive，不直接等于产品成功。
- Flow 保留为 Kotlin/Lua operation orchestration 层。
- Payload 与 Finish 独立于 backend，并由唯一落盘 root script 承载。
- Finish、Payload 与 backend cleanup 各自保留 owner；不建立通用 executor façade。
- 产品成功唯一投影为 `RunCapability.PRODUCT_ROOT + RunTerminal.SUCCESS`。
- 任何 backend 通过目标设备门之前都不标 Ready/supported。
