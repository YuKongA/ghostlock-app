# 待裁决清单（v2 · 2026-10-08 更新）

> 用法：每条 = **决定什么 / 为什么开着 / 参考（可点开核对）/ 选项与代价 / 建议**。回复编号 + 结论即可（例：A1 可以；B1 批准；其余照建议）。不点名 = 按建议执行。
> 状态含义：**阻塞** = 不定就无法开工；**本批** = 影响 B0.0/B1 设计定稿；**可延后** = 不挡主线。
> 已裁决项见文末 §F。

## §A 立即阻塞（1 项）

### A1 批准开工 B0.0 前两项：R1 契约中性化 + R2 接口边界

- **决定什么**：允许开始 B0.0 的 R1/R2（**不动机器码**：契约面、接口边界、文档与判据）。
- **为什么开着**：L 级设计需你认可后才动（AGENTS 先设计后改动）；R6/R4/R9 你已批，但排序在 R1/R2 之后（契约面决定后续判据形状）。
- **参考**：[B0.0-REVISION.md](B0.0-REVISION.md) 的 R1/R2 节 · 你 2026-10-08 对 R1 的口径（无虚基类：token + ParamSpec → op 内强类型）· 对 R2 的口径（orchestrator → composition_root：实例化 + 构造绑定 + 编译期校验，顺序归计划）。
- **选项**：① 可以（推荐）→ 立刻开工；② 先看 R1/R2 产出清单再定；③ 调整口径。
- **建议**：①。**不定就停在这里**：B0.0 其余项全部排队。

## §B 本批次要定（6 项）

### B1 批准 R11 设计（queue 退役 · 计划=脚本）

- **决定什么**：按你选 C 落成的设计稿是否批准（L 级：wire/profile 格式 + 跨语言契约）。
- **参考**：[script-plan-design.md](../analysis/script-plan-design.md)（决策五条 + 影响面 + 判据 + 批次）· 沿革 token（—2026-10-06）→ queue（2026-10-06）→ script（2026-10-08）· 退役指针 [queue-schema.md](../analysis/handoff-payload-queue-schema.md):3。
- **选项**：① 批准（推荐）；② 等 task-86 补全影响面后再批；③ 调整决策（如内置脚本形态）。
- **建议**：等 task-86 把删除面与重冻清单补全（manifest 双副本 / fixture 90 行 / golden 3920 hex / native-doc-golden 58 行 / stepset-steps / 67 个资产）后**一次批**。

### B2 确认 R11 两点前提（内置计划脚本 / route 保留）

- **决定什么**：① 每个 (backend, route) 一份**内置计划脚本**随 App 资产提供（plan-<backend>-<route>.lua），使不带自定义脚本也能跑；② **保留 backend.<id>.route**（编译期策略与几何选择，不是步骤）。
- **参考**：script-plan-design.md §1 决策 ①④⑤ · 若取消内置脚本 ⇒ **67 个资产档全要补脚本**（实测 67/72 含 queue）。
- **建议**：两者都保留。

### B3 确认「窗口内自定义 = 参数化，不拆分」条款

- **决定什么**：把「窗口内可变项一律走参数（如重试次数），不把窗口拆成多个 op」写进 R2 边界表。
- **参考**：既有窗口内参数（[execution-tuning.conf:20-36]，实测 22 行键=值(../../app/src/main/assets/profile/execution-tuning.conf)）· 循环实现（[steps.cpp:77](../../src/core/backend/cve_2026_43499/steps.cpp)、[:238](../../src/core/backend/cve_2026_43499/steps.cpp)）· 参数校验面 ParamSpec{key, wire, required, max}（[b1b2:10](../analysis/handoff-payload-b1b2-runtime.md)）· 拆分的四条代价（窗口内 Lua / 窗口内新增代码路径 / 窗口拉长 / 失败态组合爆炸）· 实证：窗口内一条 syscall 即可 panic（[pi-window-constraints.md](../analysis/pi-window-constraints.md)）。
- **建议**：确认（你的重试次数可调由此满足，且带 max 上界）。

### B4 steps 键是否与 queue 同批清掉

- **决定什么**：backend.<id>.steps 早已退休（M5），但 extractor 仍发射它，且有一条对拍断言把它钉住。
- **参考**：发射点 [report.rs:185](../../tools/extract_rs/src/report.rs) · 断言 [report.rs:1745](../../tools/extract_rs/src/report.rs) · manifest 标注（REMOVED selection surface (M5)）· native 遗留读取 [backend_profile.cpp:82-85](../../src/core/backend/cve_2026_43499/backend_profile.cpp)。
- **选项**：① 同批清（推荐，不留叠加）；② 独立小批次。
- **建议**：①。

### B5 R10 裁定（静态数据留 HOCON 还是进脚本）⇒ 裁定即关闭 R3

- **决定什么**：三形态选一（L-A 脚本载体 / L-B 脚本+schema / L-C 数据优先），R3（无默认值范围）随之定。
- **参考**：[lua-config-structure-options.md](../analysis/lua-config-structure-options.md)（四层默认值事实 / 三形态四维对比 / 判据 / 分期）· 两个先修隐患（[NativeProfile.kt:450-451](../../profile-core/src/main/kotlin/com/ghostlock/app/data/profile/NativeProfile.kt) 的零填充；multicast 无调参资产）· R3 现状 = 阻塞于 R10。
- **建议**：数据在 HOCON（脚本只读）/ 行为在 Lua（L-C 迁移路径 + L-A 行为载体），并按 A2 写明同一事实只有一处权威。

### B6 批准 ADR-0010（Lua ↔ C++ 资源模型）

- **决定什么**：句柄作用域三分类 + 八条硬规则 + 五条具名拒绝 + GC 边界是否成立。
- **参考**：[0010-lua-cpp-resource-model.md](../analysis/adr/0010-lua-cpp-resource-model.md)（Proposed）· 索引 [adr/README.md:15](../analysis/adr/README.md)。
- **建议**：批准。

## §C 流程与规范（7 项，可延后）

### C1 审查原则定稿 + A 组是否升阻断级

- **参考**：[design-review-principles.md](../development/design-review-principles.md) §1 · [AGENTS.md](../../AGENTS.md) design-critic 规则 5（阻断未关闭不得进 gate）。
- **建议**：定稿 + A 组升阻断。

### C2 design-critic 是否正式启用为长期审查岗

- **参考**：[AGENTS.md](../../AGENTS.md) design-critic 整节（9 条强制规则 + 四方分工）。
- **建议**：启用。

### C3 会话内提交计数口径（现 10/10 已满）

- **参考**：实测 origin/main..dev = 27（会话前 17 + 会话内 10）· 你此前要求本会话内 ≤10。
- **建议**：边界后移到 2026-10-08 15:00 起算新额度（或改为每批 ≤N）。

### C4 文档配额策略（两份计划文件已满）

- **参考**：MASTER-PLAN 16 378/16 384 B、B0.0-REVISION 16 383/16 384 B（余 6 B / 1 B）· 配额来源 principles A3:13。
- **选项**：① 每批次独立计划文件 + MASTER-PLAN 细节节外移只留指针（推荐）；② 放宽计划配额；③ 维持冻结。
- **建议**：①。

### C5 审查记录落档（当前只在会话消息里）

- **参考**：[AGENTS.md](../../AGENTS.md) design-critic 规则 9（记录义务）· 本会话 11+ 轮审查。
- **建议**：补 docs/analysis/handoff-payload-review-log.md（时间 / 原则版本 / 总判 / 阻断数 / 快照哈希）。

### C6 历史压缩沿革是否记档

- **参考**：686 → 17（会话前）+ 会话内 10 · 备份分支 backup/pre-squash-20261007 · dev 已推、未 force-push。
- **建议**：记一行。

### C7 文档配额的历史违规处置

- **参考**：Lead 全仓审计（2026-10-08）实测 —— **分析类共 60 件，20 件超 8192**（合计 **589998 B**；Top：`contract-design.md` 153501 / `execution-combination-menu-and-general-profile.md` 80896 / `cve-2026-43284-b5-design.md` 45771 / `compose-ui-standards.md` 40600 / `root-manager-selection-design.md` 29929）；**计划类超限 0**。
- **三者关系**：绝大多数为**既有文档**（今日仅因归档指针更新被改 mtime）；**唯一本会话产物** = `p2-review-20261008.md`（原 10254/8645 → **已压至 8182 ≤8192**；附件 3155）。
- **选项**：**A** 既往不咎 + **前行规则**（新建或被修改件必须达标）+ 既有 20 件登记为**一次性清理候选**（owner + 时机）【**建议**】；**B** 追溯拆分 20 件（约 590 KB 重排）；**C** 登记豁免（须在 principles 记沿革）。
- **与 C4 的关系（互相链）**：**C4 管「计划类余量」**（两份计划文件仅余 6 B / 1 B 的当下策略）；**本项管「分析类历史违规」的处置口径**（存量 vs 增量）。A 与 C4 的「每批次独立计划文件 + 详情外移」**同向**：都靠「**新件达标 + 指针化**」而非追溯重排。

## §D 仓库与发布（4 项）

### D1 在途 native 改动是否已有真机门禁

- **决定什么**：提交 56eac698 的 5 个源文件是否属攻击关键路径，若是是否已跑真机。
- **参考**：[AGENTS.md](../../AGENTS.md) 验证门槛 · git show --stat 56eac698 · 归档格式 [device-gates](../analysis/device-gates/)。
- **建议**：列入第一个真机窗口（不阻塞 B0.0）。

### D2 .vscode/settings.json 是否保留跟踪

- **参考**：提交 3f329118 纳入跟踪；git check-ignore 未忽略。
- **建议**：保留。

### D3 备份分支与远端旧分支

- **参考**：backup/pre-squash-20261007 = 691 提交；origin/vr-ko-bypass-dev = acc5e7ba。
- **建议**：备份留到 dev 稳定后删；旧远端分支保留。

### D4 社区短文 ANNOUNCEMENT.md 去向

- **参考**：提交 5f184760 删除该文件 46 行；文本已在会话消息里交付。
- **建议**：不恢复文件。

## §E 批量项（1 项）

### E1 D1–D32 设计默认是否有要推翻的

- **参考**：[handoff-payload-plan.md](../analysis/handoff-payload-plan.md) §10（D 表）· [decisions](../analysis/handoff-payload-decisions.md) 与 [-archive](../analysis/handoff-payload-decisions-archive.md)。
- **建议**：全部按默认通过（不点名 = 通过）。

## §F 已裁决（供核对）

| 时间 | 事项 | 结论 |
|---|---|---|
| 2026-10-08 | queue 与脚本的权威 | 选 C：queue 退役，计划 = 脚本（Lead 提的 B 被否） |
| 2026-10-08 | R5 窗口内 sched_setattr | 延期（触发：B0.1→B3 完成后评审），保持现状 |
| 2026-10-08 | R4 / R6 / R9 | 已批准 |
| 2026-10-08 | requirements 缺失条文（F1–F20、Q1–Q9） | 暂不补写，引用处标条文待补 |
| 2026-10-08 | 路线图时间窗 | 节奏制、不写时间 |
| 2026-10-07 | 导入策略 | 无限制导入 + 沙箱保留 + 硬线 D29/D30 |
| 2026-10-07 | 队列取代 token | 已裁决（M5 实施） |

---

## §G 开工就绪状态（无需裁决）

| 项 | 状态 |
|---|---|
| 分支 | dev（origin/main..dev = 27 提交） |
| 工作树 | 干净（0 条未提交） |
| 门禁 | host 测试 EXIT=0 · Gradle 单测 BUILD SUCCESSFUL · markdownlint 167 文件 0 issue |
| 备份 | backup/pre-squash-20261007（691 提交） |
| 团队 | native-core / kotlin-app / docs-uml / design-critic（除 task-86 在写文档外全部空闲） |
