# 未关闭的Payload编辑与展示需求

## Context and scope

完整迁入的有效设计/范围与验收正文；不是新增实施批。原批准/历史结果按原Beads记录，工具工件齐备不代表实现或门禁通过。

来源：`043fdf6a:.beads/issues.jsonl`；保留语义代号和原锚点。本文中的历史进度/门结果文字仅是其具名输入的沿革，当前状态从proposal列出的Beads读取。旧机制由正文已注明的后继差异取代；基线不重新授权实施。

## Decisions

<a id="record-gl-4uv"></a>

## P-PAYLOAD-CORE-001/需求01：Root Manager 默认项具名 Incomplete

<!-- migrated-source: 043fdf6a Beads gl-4uv description -->
默认项在未检测到Root Manager时显示具名Incomplete。已有通用resolver/ack/fail-closed不等于该UI判据完成，尚无关闭证据；不并入B1或构建修复。

<!-- migrated-source: design gl-4uv -->
- 未检测到 Root Manager 时，默认启动 Root Manager 项显示具名 Incomplete。

原始来源：f882e670:docs/plan/developer-defined-tasks.md；原未勾状态保留，不推断源码尚未实现。相关已完成设计：gl-kct, gl-zgp

<a id="acceptance-gl-4uv"></a>

### 验收定义与原边界：gl-4uv

<!-- migrated-source: acceptance_criteria gl-4uv -->
- 未检测到 Root Manager 时，默认启动 Root Manager 项显示具名 Incomplete。
按实际行为证据关闭；当前未有上述未证事实的验收记录，NOT_RUN。

批准/审核/实际结果：Beads `gl-4uv` notes；本段不维护实时勾选或新PASS。

<a id="record-gl-orc"></a>

## P-PAYLOAD-IO-001/需求01：Payload 编辑与纯文本 Preview 验收

<!-- migrated-source: 043fdf6a Beads gl-orc description -->
保留删除底部伪Save、右上角Payload-only Preview、返回纯文本且Finish零读取三条原需求。Payload-only数据链和Finish零读取已有source/测试记录；按钮去除/位置和纯文本返回尚无明确关闭证据。不重复实现已接通链，也不因String返回类型声称纯文本验收。

<!-- migrated-source: design gl-orc -->
- 删除 Payload 页面底部的伪 Save。
- Payload 页面右上角增加 Payload-only Preview。
- Preview 返回纯文本且对 Finish 零读取。

原始来源：f882e670:docs/plan/developer-defined-tasks.md；原未勾状态保留，不推断源码尚未实现。相关已完成设计：gl-zgp, gl-k6e

<a id="acceptance-gl-orc"></a>

### 验收定义与原边界：gl-orc

<!-- migrated-source: acceptance_criteria gl-orc -->
- 删除 Payload 页面底部的伪 Save。
- Payload 页面右上角增加 Payload-only Preview。
- Preview 返回纯文本且对 Finish 零读取。
按实际行为证据关闭；当前未有上述未证事实的验收记录，NOT_RUN。

批准/审核/实际结果：Beads `gl-orc` notes；本段不维护实时勾选或新PASS。

<a id="record-gl-gyw"></a>

## P-PAYLOAD-IO-001/需求02：代码编辑器依赖 Spike 与选型

<!-- migrated-source: 043fdf6a Beads gl-gyw description -->
现有主计划/剩余快照未提供选型或关闭证据；保留原需求，未自动纳入B1、构建批或生产实施授权。

<!-- migrated-source: design gl-gyw -->
- 完成代码编辑器依赖 Spike 与最终选型。

原始来源：f882e670:docs/plan/developer-defined-tasks.md；原未勾状态保留，不推断源码尚未实现。相关已完成设计：gl-zgp

<a id="acceptance-gl-gyw"></a>

### 验收定义与原边界：gl-gyw

<!-- migrated-source: acceptance_criteria gl-gyw -->
- 完成代码编辑器依赖 Spike 与最终选型。
按实际行为证据关闭；当前未有上述未证事实的验收记录，NOT_RUN。

批准/审核/实际结果：Beads `gl-gyw` notes；本段不维护实时勾选或新PASS。
