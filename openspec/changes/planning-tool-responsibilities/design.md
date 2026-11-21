## Context and scope

用户已明确授权这次职责纠正，不撤销上轮有效内容迁移、已迁任务或归档，也不整批git revert/reset。设计正文只在OpenSpec，Beads不再保存第二份design/完整验收矩阵；现行工程治理规范保持原位和原权威。

## Decisions

- 以043fdf6a的52条Beads记录为迁移输入。完整正文按实际主题归入OpenSpec change，Beads spec_id直达相应design及稳定语义锚点。既有decision.closed只表示设计收录，不据此归档未验收的B1、DataStore或自动导出。
- 插件父架构与B1分别维护共享设计/当批exact范围；后继需求只引用父架构。日志v3、LogConnector及新增MediaStore保留行为一并迁入，未关闭gl-f47继续活动。
- 公共执行、RunHost、Diagnose/handoff等共享基线按主题放入OpenSpec设计，明确基线与后继delta，不作为新实施批或新增任务。真正已完成的文档工具批仅保存历史设计，原试点证据留Beads。
- 公共RunHost wire正文移入openspec/specs，原数值/状态机/边界保持；B1的PFD+semantic-root增量放所属change specs，不把未完成增量发布为已实现规范。其它尚未细化需求不为工具补造完整spec。
- 保留原批准、critic、自检、运行命令及输出；设计中的历史状态仅为具名输入快照，实时状态只在Beads。混合notes中的设计/编号映射迁到OpenSpec，真实证据仍可由原ID读取。
- 恢复固定OpenSpec2.1.1与已有6个按职责skills，schema继续tracks:null；不恢复tasks.md、旧Markdown计划或跨工具双写。CDA/作者自检/critic从spec_id定位同一正文，只按任务读。

## Verification and authorization

用户已批准本次分工纠正；既有业务批准不变。作者检查真实读取链AGENTS→skill→Beads spec_id→OpenSpec正文→原合同/证据；固定非作者critic只审职责变更及保真风险，不重开业务审批。

适用验证：OpenSpec原生validate/status/apply无Markdown tasks；3类工件/当前wire与B1增量可解析；52条原ID/status/依赖不变、完整正文与验收均有唯一OpenSpec落点、Beads无第二设计正文；批准/证据保留，直接链接有效；原生Beads export/bootstrap保持指针和状态；skills验证与Git范围检查。生产构建/AVD/真机本轮NOT_RUN，不改原门。
