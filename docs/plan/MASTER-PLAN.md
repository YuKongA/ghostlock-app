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
| P-BACKEND-CORE-001 | BackendSpec 公开描述合同；定义仍在统一计划内 | [BackendSpec 公开描述合同](backend-root-execution-unification-plan.md#p-backend-core-001) |
| P-BACKEND-X-001 | Backend 成员资格、删除与断开闭包；定义仍在统一计划内 | [Backend 成员资格、删除与断开闭包](backend-root-execution-unification-plan.md#p-backend-x-001) |
| P-RUN-CORE-001 | 运行结果、会话终态与内部 core 收口；定义仍在统一计划内 | [运行结果、会话终态与内部 core 收口](backend-root-execution-unification-plan.md#p-run-core-001) |
| E-DOC-X-001 | 计划权威、归档与跨文档入口收口；定义仍在统一计划内 | [计划权威、归档与跨文档入口收口](backend-root-execution-unification-plan.md#e-doc-x-001) |
| P-LOG-IO-001 | 日志自动导出与文件 sink；定义仍在统一计划内 | [日志自动导出与文件 sink](backend-root-execution-unification-plan.md#p-log-io-001) |
| P-LOG-X-001 | 跨层单一日志中枢及结果归属边界；定义仍在统一计划内 | [跨层单一日志中枢及结果归属边界](backend-root-execution-unification-plan.md#p-log-x-001) |
| P-DIAG-NATIVE-001 | Diagnose 的 native／JNA／shell 诊断纵向；定义仍在统一计划内 | [Diagnose 的 native／JNA／shell 诊断纵向](backend-root-execution-unification-plan.md#p-diag-native-001) |
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

- E-DOC-X-001/批次01/P-BACKEND-X-001/批次01：已完成 authority 收口与 disabled/original closure 删除，并保留当批通过记录。
- P-RUN-IO-001/批次01：Payload/Finish/root-script source implementation 已落地；新增 fault matrix、AVD 与设备证据按用户指令保持 NOT_RUN。
- P-RUN-X-001/批次01：neutral BackendSpec、typed RunHost、App/Shizuku clients、RunReport 与 UI source implementation 已落地；compile、App RunHost AVD smoke与用户观察的Shizuku shell-domain AVD production runtime均已PASS，真实设备gate仍为NOT_RUN。
- P-RUN-CORE-001/批次01：caller-zero SessionPhase 与 source guard 已落地；automatic export 被误判为 dead closure 的处置已由用户纠正，P-RUN-CORE-001/批次01 因而重新打开。
- P-LOG-IO-001/批次01→P-LOG-IO-001/批次02→P-LOG-X-001/批次01：P-LOG-IO-001/批次02 的 single-channel/single-worker automatic-export sink 保留；P-LOG-X-001/批次01 已通过 design-critic，将 Kotlin/Native/Shell/RunHost 日志统一交给 `RunCoordinator` 单一中枢，并把 `RunReport` 组合归还 Flow。旧 event-as-log、独立 report UI 与 legacy `app/log` 中枢已删除。current source compile、96项 unit XML、architecture、androidTest compile、native host与Android zero-target gate均已 PASS；P-LOG-X-001/批次01 current-tree AVD UI/runtime 与真实设备/final gate仍为 NOT_RUN。
- P-BACKEND-X-001/批次02：后续用户裁定取代原 carrier 计划；S2 native与S3 plugin均取消。所有真攻击 backend 已从 Kotlin registry、Gradle开关/任务与APK native target graph断开，旧native实现保持source-only且不修改。S1 neutral common source仍无真实backend caller，不代表接线完成。
- P-RUN-IO-001/批次01 至 P-LOG-X-001/批次01（原批次范围） evidence debt已开始偿还：P-LOG-IO-001/批次02 host/Android build/exact instrumentation/AVD MediaStore与旧 Shizuku runtime已有历史证据；P-LOG-X-001/批次01 wire/UI变更后的 current-tree AVD runtime证据仍待取得。P-RUN-IO-001/批次01 fault matrix与真实设备仍待执行。当前没有 P-BACKEND-X-001/批次02/P-BACKEND-NATIVE-001/批次01/P-BACKEND-NATIVE-002/批次01 真实backend实施批，P-DIAG-NATIVE-001/批次01只在真实 shared operation consumer重新获批后启动。

## 固定边界

- `raw uid 0` 只证明 backend primitive，不直接等于产品成功。
- Flow 保留为 Kotlin/Lua operation orchestration 层。
- Payload 与 Finish 独立于 backend，并由唯一落盘 root script 承载。
- Finish、Payload 与 backend cleanup 各自保留 owner；不建立通用 executor façade。
- 产品成功唯一投影为 `RunCapability.PRODUCT_ROOT + RunTerminal.SUCCESS`。
- 任何 backend 通过目标设备门之前都不标 Ready/supported。
