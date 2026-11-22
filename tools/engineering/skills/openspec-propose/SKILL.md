---
name: openspec-propose
description: 在本项目新建需要计划的设计变更时使用，尤其新 L 级设计；普通 S/M 不补建 change，已有设计修订或实施转对应 skill。
license: MIT
metadata:
  author: openspec
  upstream-version: "2.1.1"
  project-adaptation: "ghostlock-beads"
---

# 新建设计变更

先按[改动分级](../../../../docs/development/engineering-standards.md#engineering-change-levels)选路：普通 S/M 沿原任务处理，M 用简短改动说明，不为使用工具新建 change；已有设计修订转 [update](../openspec-update-change/SKILL.md)，按已批准设计实施转 [apply](../openspec-apply-change/SKILL.md)。新 L 必须形成设计；不能按代码行数或“小修”标签降级。

先从 OpenSpec 当前 change 与工程代号入口定位已有主题，避免重复设计。`npm --prefix tools/engineering run --silent openspec -- new change <name>` 使用项目 ghostlock schema；按 `status --change <name> --json` / `instructions <artifact> --change <name> --json` 创建当前需要的 proposal/design/specs。

完整目标、范围、技术设计和验收定义只写 OpenSpec；共享合同链接现行 OpenSpec 唯一位置，不用旧 Markdown 或 Beads design 字段代替正文。真正无行为 delta 的变更用 skip_specs:true，待分析需求不补造完整 spec。

实质架构使用 [CDA](../constraint-driven-architecture/SKILL.md)推导并直接写同一份设计；非架构 L 不强制 CDA。正式送审前先做[作者自检](../ghostlock-author-self-check/SKILL.md)；**新 L 定稿必须按 [AGENTS §7](../../../../AGENTS.md#agents-review)由 Lead 委派非作者 [design-critic](../ghostlock-design-critic/SKILL.md)**，与是否用了 CDA 无关。对应阻断按原条件关闭并取得原要求的用户批准后，才转 apply；工件齐备不代替这两道门。Beads 执行任务按共同分工关联正文，不为审查本身制造前置任务。

共同分工只见[文档规范](../../../../docs/development/documentation-standards.md#planning-tool-authority)；安装与命令只见[开发环境](../../../../docs/development/development-environment.md#planning-tools)。
