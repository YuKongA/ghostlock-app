> **历史归档，非当前计划入口** · 原路径：`openspec/changes/engineering-workflow/design.md` · 日期：2026-11-21 · 原因：有效设计/批准/验收完整迁入 Beads，旧计划/OpenSpec退出执行 · 来源：`f882e670:openspec/changes/engineering-workflow/design.md` · 后继：Beads（gl-ind为迁移记录；具体主题从工程代号入口直达）。下方原状态、指令、checkbox及链接仅按当时语境解释，不恢复被取代设计或当前批准。

## Context and scope

EVOLUTION。用户已选择工具组合并授权安装/接入/试点/活跃计划迁移；原产品计划批准不被该授权扩大。范围限流程、文档、配置和相关测试。固定 OpenSpec2.1.1、Beads1.3.4；CDA 来源 commit `04d2e998f19b08be6ba7f697315f3a16ecdf2708`。

VERIFIED：BXP3-B1 批准与 P0 已关闭，I/V1/V2/V3 尚未关闭；当前148行清单位于原计划 §19.5.70，不是旧240行 TSV。旧未勾选项包含被取代设计；不能自动全部导入。开发环境实际接线问题见[环境文档](../../../../development/development-environment.md)。

## Decisions

1. **一类信息一处维护**：工程规则保留 docs/development；设计变更由 OpenSpec 定位。既有计划全文保留原处，迁移 change 仅链接当前有效章节、批准和验收；新设计在 change 中编写，不再另写平行长计划。Beads 只维护可执行任务/候选、依赖、认领、运行证据和关闭状态；不得把描述摘要当第二份批准。原计划进度和 MASTER/ROADMAP 改为直达链接，旧证据明确为迁移前快照。
2. **用原生能力而非同步系统**：项目 schema 保留 proposal/specs/design，`apply.tracks: null` 删除 Markdown tasks authority。无产品合同变化使用上游 `skip_specs`；只对真正行为变更维护 delta specs。Beads `spec_id`/`external_ref` 指向设计与原语义编号，生成 ID 不替代人类语义码。
3. **保存与协作**：Beads 使用嵌入式 Dolt，无常驻服务器。CLI 是唯一写入入口；`.beads/issues.jsonl` 是原生命令生成的可携带任务投影，禁止手编，不是第二个任务表或完整数据库备份。`bootstrap` 支持 fresh clone 导入；跨机器并发同步使用原生 Dolt remote，须获准发布后再启用，不用 JSONL 双向合并。关闭工具不经范围核对的自动 push、Git stage、backup 和 hooks；Dolt 内部事务历史不等于源码 Git commit。首次 init 自动创建本地 Git 提交 `cebb1804`，已向用户报告；用户于2026-11-21纠正旧提交限制，完成并验证本批后由 Agent 主动精确提交，不再等用户手动催促，不擅自重写历史。后续用 bootstrap，不重复 init。
4. **任务状态不是批准或 PASS**：`ready` 只说明依赖无阻塞。待设计/未批准候选使用 deferred，缺具名条件的任务 blocked；close 必须说明完成/取代/取消及对应证据，不把取消写 PASS。迁移只导入有效剩余工作，不重开已完成批次；未来需求不阻断无关当前批次。
5. **保留原职责**：CDA 只用于实质技术设计，结论写原 design；作者自检仍修原交付，critic 仍非作者只读。工具 schema/格式检查属机械核对。原批准与真实设备门不由 artifact exists、all_done、Beads close 或工具升级替代。

## Verification and authorization

- 先用现有“按任务配置开发环境”流程变更跑通：创建真实交付任务和验收任务 → 依赖阻止提前 ready/close → claim → 完成文档与链接/版本检查 → close → 后继 ready。保存证据到原任务，不建测试报告。
- 检查 schema/apply 不生成 tasks.md；修改 Beads 状态不要求更新 OpenSpec 进度。重新打开 CLI 核对状态持久化；检查导出的依赖、证据、原语义码和 NOT_RUN 没有丢失。
- 试点通过后迁移活跃批次，批准正文、当前148行 scope、验收矩阵不改。仅一次核对取代关系、任务依赖、未批准事项不可执行及原门保持 NOT_RUN；其它聊天仍在实施 B1，落笔前重读对应状态，不替实施者认领或写生产代码。
- 本轮不运行产品构建/AVD/真机门；这些均 NOT_RUN。安装成功不证明工程配置或产品行为通过。

作者自检：已核真实使用者（作者、执行 agent、新 clone）、工具实际 CLI、状态与设计边界及初始迁移清单。

独立审查（2026-11-21，`/root/workflow_critic`）：设计阻断0，可进入既定接入与试点。Lead 核对实际 schema/skill 切换、JSONL 最后更新与活跃实施者的状态交接，不再追加 critic PASS；本结论不证明试点或产品门通过。原实施聊天 `01a1ec39-4b71-7911-a080-05c1b7ae5fa7` 当前为 idle，最新用户优先项为构建闭环修复，精确设计待批准；迁移必须保留这一更新，不继续把 B1 实施描述为唯一立即执行项。
