# 待裁决清单（v3 · 2026-10-08 更新）

> 用法：每条 = **决定什么 / 参考 / 选项与代价 / 建议**。回复编号 + 结论即可（例：A1 裁定 X；其余照建议）。不点名 = 按建议执行。
> 分组：**§A 阻塞**（不定则某层无法开工）· **§B 设计与规范**（本批/近期）· **§C 账目与仓库** · **§D 批量默认** · **§E 已闭环**（供核对，不重复决定）。

## §A 阻塞项（3 条，全部已经多轮等待）

### A1 R10：静态数据（几何/调参）留 HOCON 还是进 Lua —— 解锁 **P3**

- **参考**：[lua-config-structure-options.md](../analysis/lua-config-structure-options.md)（四层默认值实测 + 三形态四维对比 + 两个先修隐患）· [composition-layer-p4-spec.md](../analysis/composition-layer-p4-spec.md) §9 未决 · P3 规格尚未撰写
- **选项**：① L-C 数据在 HOCON（脚本只读）+ 行为在 Lua【建议】；② L-A 脚本载体；③ L-B 脚本 + 显式 schema（schema 取自 OpRegistry 则无重复描述成本）；④ 先修两个隐患（Kotlin `?: 0u` 零填充 / multicast 无调参资产）再定
- **建议**：① + ④（隐患与形态无关，先修）

### A2 批准 ADR-0008 / 0009 / 0010 —— 解锁 **P5**（vendor Lua + 沙箱 + 句柄表）

- **参考**：[ADR-0008](../analysis/adr/0008-lua-runtime-sandbox.md)（体积数字已复现：109 620 B / 88 264 B，≈107 KB ≈ 2.2%；Berkeley 口径警告已写入）· [ADR-0009](../analysis/adr/0009-script-trust-and-hardline.md) · [ADR-0010](../analysis/adr/0010-lua-cpp-resource-model.md) · [P5 规格](../analysis/handoff-script-layer-p5-spec.md)（组件边界 + 判据 + 两证伪）
- **选项**：① 三件一起批准【建议】；② 逐件批；③ 先批 P5 规格（R11 设计含 queue 退役）再批 ADR
- **说明**：Lua 源码仓内没有，需**下载 vendored 到 `src/lib/lua/`**（lua.org 可达，lua-5.4.7.tar.gz 374 097 B，sha256 前 16 = `9fbf5e28ef86c698`）；ADR 要求登记来源与许可（I12）

### A3 真实样本（boot.img / OTA）—— 解锁 **extractor ↔ Kotlin 产物级对拍**（R8 缺口③）

- **参考**：[host-assembled-integration-plan.md](../analysis/host-assembled-integration-plan.md) §3 缺口③ · 仓内实测无 `.img`/OTA 样本
- **选项**：① 你提供一份样本（或指定镜像 URL）；② 用合成/最小夹具替代（判据降级，需登记）；③ 先跳过，等 E2E-1
- **建议**：①（最省且判据不降级）

## §B 设计与规范（4 条，本批/近期）

### B1 批准 R11 设计（queue 退役 · 计划 = 脚本）

- **参考**：[script-plan-design.md](../analysis/script-plan-design.md)（决策五条 + 影响面 + 重冻清单 + 判据 + 批次）· 你已裁定选 C（queue 退役）
- **建议**：批准（它是 P5 的输入；内置计划脚本命名 `plan-<backend>-<route>.lua`）

### B2 确认两点前提：内置计划脚本随资产 / 保留 `backend.<id>.route`

- **参考**：同上 §1 决策①④⑤；取消内置脚本 ⇒ **67 个资产档全要补脚本**
- **建议**：两者都保留

### B3 确认「最小化时机重排」（Lead 已裁决，请追认）

- **参考**：[p2-minimisation-plan.md](../analysis/p2-minimisation-plan.md) §0（2026-10-08 重排）· [rewrite-plan.md](rewrite-plan.md) §8.1 U-4
- **内容**：B-1..B-3（24 项）**曾执行 → 违反 R39 → 全部回退 → 净效果 0**；最小化**整体后移到 P3/P5 落地且 `.disabled` 参考件退役之后**；此前凡被 `.disabled`/`docs` 引用者 = **预留接口，不得删**
- **建议**：追认（理由：参考件是新实现的输入规格；当前可删面 = 0）

### B4 `steps` 键（M5 已退休）是否与 queue 同批清

- **参考**：extractor 仍发射 [report.rs:185](../../tools/extract_rs/src/report.rs) + 断言 `:1745`；native 遗留读取 [backend_profile.cpp:82-85](../../src/core/backend/cve_2026_43499/backend_profile.cpp)
- **建议**：与 R11/P5 的 wire 变更同批清

### B5 审查原则定稿 + **A 组是否升阻断级**；`design-critic` 是否正式启用

- **参考**：[design-review-principles.md](../development/design-review-principles.md) §1（A1 / A2 / A3…）· [AGENTS.md](../../AGENTS.md) design-critic 节（9 条规则，已在实战中使用 4 轮）
- **建议**：定稿 + A 组升阻断 + 正式启用该岗位

## §C 账目与仓库（7 条）

### C1 历史配额违规处置（C7 旧号）

- **参考**：审计实测 —— 分析类 60 件中 **20 件超 8192**（合计 589 998 B；计划类 0 超限）；本会话产物仅 `p2-review-20261008.md`（已压到 8182）
- **选项**：① **既往不咎 + 前行规则**（新建/被修改件必须达标 + 既有 20 件登记为一次性清理候选，owner+时机）【建议】；② 追溯拆分（≈590 KB 重排）；③ 登记豁免（须记沿革）

### C2 会话内提交计数口径（早已远超 10）

- **参考**：`git rev-list --count origin/main..dev` = **29**（会话前 17 + 会话内 12）；自基线以来 15 个提交
- **选项**：① 该约束**作废**（改成「每批一次提交」）【建议】；② 再压一次历史；③ 放宽上限

### C3 历史压缩沿革是否记档（686 → 17 + 会话内 N；备份分支；未 force-push）

- **参考**：`backup/pre-squash-20261007` = 691 提交；`dev` 已推
- **建议**：记一行（analysis 或 MASTER-PLAN §10）

### C4 在途 native 改动是否已过真机门禁

- **参考**：提交 `56eac698` 的 5 个文件（`backend_terminal.*` / `execution_binding.cpp` / `glkv3_parse.cpp` / `main.cpp`）；[AGENTS.md](../../AGENTS.md) 验证门槛
- **建议**：列入第一个真机窗口（不阻塞当前）

### C5 `.vscode/settings.json` 是否保留跟踪

- **参考**：提交 `3f329118` 纳入跟踪；未被 .gitignore 忽略
- **建议**：保留

### C6 备份分支 / 远端旧分支去留

- **参考**：`backup/pre-squash-20261007`（691 提交）· `origin/vr-ko-bypass-dev` = `acc5e7ba`
- **建议**：备份留到 `dev` 稳定后删；旧远端分支保留作历史锚点

### C7 社区短文 ANNOUNCEMENT.md 去向

- **参考**：你的提交 `5f184760` 删除该文件 46 行；文本已在会话中交付过
- **建议**：不恢复文件（需要留档就放 `docs/`）

## §D 批量默认（1 条）

### D1 D1–D32 设计默认是否有要推翻的

- **参考**：[handoff-payload-plan.md](../analysis/handoff-payload-plan.md) §10（D 表）· [decisions](../analysis/handoff-payload-decisions.md) / [-archive](../analysis/handoff-payload-decisions-archive.md)
- **建议**：全部按默认通过（不点名 = 通过）

## §E 已闭环（供核对）

| 项 | 结论 |
|---|---|
| 设计开工（R1/R2） | 已批准并**已执行**（P4 里程碑 A/B 全绿、已入库） |
| R11 选型 | **选 C**：queue 退役、计划 = 脚本 |
| 窗口内自定义 | **参数化不拆分**（P4 规格已落，`ParamSpec.max` 为上界） |
| R5 窗口内 sched_setattr | **延期**（触发：B0.1→B3 完成后评审） |
| R4 / R6 / R9 | 已批准并落地（E4 实证版 / E3 重写 / 文档三层拆分） |
| 审查记录落档 | 已完成（p2-review-20261008.md + annex，含 Lead 稽核更正节） |
| requirements 缺失条文 | 暂不补写（引用处标「条文待补」） |
| 路线图时间窗 | 节奏制、不写时间 |
| 导入策略 | 无限制导入 + 沙箱保留 + 硬线 D29/D30 |
| 最小化批次 B-1..B-3 | 执行后判定违反 R39 ⇒ **全部回退**（净效果 0，无提交涉及删除） |

---

## §F 开工就绪状态（无需裁决）

| 项 | 状态 |
|---|---|
| 分支 / 工作树 | `dev` · 干净（未提交 0） |
| 门禁 | `gate` 两段 EXIT=0（compat-check 97/0 · window-exit-paths · plan-gate）· markdownlint 0 issue |
| 已完成层 | P1 禁用扫描 ✅ · P2 兼容层 ✅（含评审闭环与计数归一）· P4 装配/窗口/门禁 ✅（里程碑 A/B 入库） |
| 备份 | `backup/pre-squash-20261007`（691 提交）· 基线 tag `legacy/baseline-20261008` |
| 团队 | native-core / kotlin-app / docs-uml / design-critic（当前 docs-uml、native-core 有收尾消息在途） |
