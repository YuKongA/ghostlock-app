# 文档编写规范（Documentation Standards）

> 本仓库一切文档的写法。风格参照 Google Developer Documentation Style Guide：短、直接、面向读者；
> 内容分类参照 Diátaxis。来源见文末。
> 效力：用户要求 > `AGENTS.md` > 本文；与代码冲突时以代码为准，并顺手改文档。

## 先定类型

写之前回答两个问题：**给谁看？回答什么问题？** 然后在表里选一类，一篇文档只属于一类。

| 类型 | 写什么 | 放哪里 |
|---|---|---|
| 教程 / 快速开始 | 带人跑通一次 | `README.md`、`docs/kernel_profiles/README.md` |
| 操作指南 | 完成一个任务 | `docs/development/*` |
| 参考 | 字段、命令、结构 | `docs/kernel_profiles/PROFILE_SCHEMA.md`、`defaults.md`、`src/core/README.md` |
| 解释 | 为什么这样设计 | `design-philosophy.md`、`engineering-standards.md`、`docs/analysis/**`（历史） |
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
- 链接用相对路径；跨文档引用写文件名，不写章节号。

## 画图

关键结构与流程**必须**画图。文字描述控制流要求读者在脑中重建拓扑；图直接给出拓扑，并让悬空的调用、
缺失的清理路径和矛盾的边界一眼可见（画图过程本身就是设计审查）。

- **什么时候必须画**（L 级改动）：新增/修改 route、跨层契约（Kotlin↔native）、会话与资源所有权、
  攻击阶段机、状态与清理/回滚边界。
- **图种**：结构（类、持有关系）用类图或组件图；控制流用流程图/时序图；状态与出口用状态图；
  数据流用 flowchart。
- **用 Mermaid**：文本化，可进 git、可评审、可 diff、可随代码更新；图更新与代码同一批次。
- **一图一个关注点**（ISO/IEC/IEEE 42010 的 view/viewpoint 思路）：图给拓扑，正文给理由与例外，
  两者不互相抄。
- **一个结构只有一处权威图**，其他文档链接它（沿用历史 `routes.md` "核心维护图唯一强制更新"的做法）。

## 双语

`README.md` / `README_ZH.md` 与 `docs/kernel_profiles/**` 的 `_ZH` 必须同批次修改，不允许单边漂移。
`docs/development/**` 用中文；代码注释与提交信息用英文。

## 模板

**计划（L 级改动必须）**

```markdown
# <主题> 计划（<YYYY-MM-DD>）
## 现状与基线        # 分支、commit、当前行为、已知问题
## 目标与约束        # 非目标明确列出
## 改动清单          # 逐文件：文件 → 改动 → 理由
## 数据流/控制流差异  # 新旧对照；不变量说明
## 兼容性与回滚
## 验证矩阵          # 每批次的命令与预期结果
## 明确保留          # 不动的部分及理由
## 进度              # [ ]/[x] 逐项，完成即更新
```

**门禁记录（真机验证必须）**

```markdown
# <编号> 真机门禁：<主题>（<route>）— PASS/FAIL
对应提交 `<hash>`（`<commit message>`）
## 设备与入口   # 型号、kernel uname -r、入口、uid/selinux/seccomp、home
## 结果         # route_done 计数与 status/clean/step/errno、child is root、handoff、panic 与否
## 日志         # 文件名（与设备 Download/GhostLock/<时间>/ 对应）
## 变更说明     # 本次验证针对的行为差异
```

编号前缀沿用 S/CPP/U01/NS*/PROFILE-*；**失败与通过同等归档**。失败记录写清影响（panic/重启/时长）、
根因与行动项，不追责——按事后剖析（postmortem）的字段写。

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

来源：Google Developer Documentation Style Guide、Diátaxis、OASIS DITA（信息类型）、
Carroll Minimalism、UML（OMG / ISO/IEC 19505）、ISO/IEC/IEEE 42010 / 15289 / 2651x、
ADR/MADR、RFC 2119、GB/T 15834、Software Engineering at Google（Henderson, 2017）。
