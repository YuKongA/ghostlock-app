# 需求与裁决登记（requirements）

> **重建声明（2026-10-07）**：本文件原版在 **2026-10-07 22:44** 的写操作事故中**被截断且不可恢复**（未跟踪文件 ⇒ git 无对象、无副本、本机 TM 快照时点更早；取证见 `docs/analysis/incident-20261007-agents-requirements-revert.md`）。以下内容**尽力重建**：
> - **重建来源**：① `/tmp/forensics-task67/requirements.md`（**966 B 尾部残片**：U5–U7 + 第 8 节来源索引，**逐字抄录**）；② 全仓引用反推（`docs/analysis/resolved-profile-registry-plan.md:20/:179`、`docs/analysis/handoff-payload-batch-plan.md:48-49`、`docs/analysis/handoff-payload-kotlin-io.md:26`、`docs/analysis/adr/0008:30`、`docs/plan/MASTER-PLAN.md`）；③ Lead 给定的编号含义清单。
> - **确定性标注**：**[确定]**（有物证：残片或显式引用）｜**[推断]**（由引用反推，措辞可能与原版不同）｜**[缺失]**（原文不可恢复，**只保留编号行，不编造条文**）。
> - **重建人/时间**：docs-uml，2026-10-07（task-71）。**请 Lead/用户核对补全；缺失项须重新编写后再批准。**

> **效力**：与守则冲突 ⇒ 以守则为准；与 `AGENTS.md`/`docs/plan/MASTER-PLAN.md` 内的需求性条款冲突 ⇒ 以本文件为准（那些条款视为副本，待同步）。
> **来源**：本会话用户原话 + `AGENTS.md`（含用户裁决标注）+ `docs/plan/MASTER-PLAN.md`。
> **编号占用注记**：本表号段已被占用（F1–F29 · I1–I14 · Q1–Q10 · U1–U7），**新增需求不得复用**；新编号从下一可用号开始。

## 1. 功能需求（F）

| 编号 | 需求 | 确定性 | 来源/依据 |
|---|---|---|---|
| F1–F20 | **原文不可恢复**（编号已占用；含义不明） | **[缺失]** | 说明：`docs/analysis/adr/0001`/ `adr/0004` 里的「F1–F18」是**架构迁移/审查项的另一套编号**，**不能**作为本表 F1–F20 的证据 |
| F21 | 配置设计族：**三层同形** | **[确定]** | `resolved-profile-registry-plan.md:20`（+ `batch-plan:48`） |
| F22 | 配置设计族：**单一权威读入口** | **[确定]** | 同上 |
| F23 | 配置设计族：**校验去重** | **[确定]** | 同上 |
| F24 | **注册套件**（三接口 + 五注册服务；私有参数进 document；**只加载一条路径**） | **[确定]** | 同上 |
| F25 | **Lua 运行期**（D22：Lua 5.4；沙箱/预算/句柄） | **[确定]** | `batch-plan:49` + `adr/0008` |
| F26 | **导入无限制**（安全责任转移；配合 I13/D29） | **[确定]** | `batch-plan:49` |
| F27 | **硬线 D29**（能力面不得含块设备 / 任意路径写） | **[确定]** | 同上 |
| F28 | **句柄 D30**（`Handle{kind,index,generation}`；脚本不可伪造地址） | **[确定]** | 同上 |
| F29 | **`bin` 仅脚本键**（插入段 ≤64 KB；最终 `.sh` 由 native 模板合成） | **[确定]** | 同上 + `kotlin-io:26` |

## 2. 接口/实现约束（I）

| 编号 | 约束 | 确定性 | 来源/依据 |
|---|---|---|---|
| I1–I8 | **原文不可恢复**（编号已占用；含义不明） | **[缺失]** | 已知：`I2` 原为「v1/v2 → 3」（`registry-plan:179`）；`I6` 与「禁止新增 CLI/旁路」相关（`MASTER-PLAN:18` 引用 `I6`/`R13`）—— 二者标 **[推断]**，其余缺失 |
| I9 | **组合权威 = `kCombinationCatalog`（唯一）+ catalog 编译期投影** | **[推断]** | `registry-plan:21`（自有规则①）+ Lead 清单 |
| I10 | `required` = **owner 被选中时必填**（改 `PROFILE_SCHEMA:13`） | **[确定]** | `registry-plan:20/:106` |
| I11 | **会话帧独立 API** | **[确定]** | `registry-plan:20` |
| I12 | **Lua vendor 来源与许可登记**（隔离告警） | **[确定]** | `adr/0008:30` |
| I13 | **沙箱参数**（白名单 / 指令预算 / 内存上限 / 单脚本 64 KB / 仅操作间运行）= **执行期硬约束**，profile 不得放宽 | **[确定]** | `batch-plan:49` + `kotlin-io:26` |
| I14 | **`bin` 键级限制**（只有脚本键接受 `bin`） | **[确定]** | `batch-plan:49` |

## 3. 文档与流程问题（Q）

| 编号 | 问题/裁决 | 确定性 | 来源/依据 |
|---|---|---|---|
| Q1–Q4、Q6–Q9 | **原文不可恢复**（编号已占用） | **[缺失]** | 仅知 `Q1` 出现过（3 处引用），含义不明 |
| Q5 | **禁止新增 CLI/旁路**；实验走**生产路径**（参数化 / 去限制） | **[推断]** | `AGENTS.md`「改动流程」末条（用户指令 2026-10-05）+ `MASTER-PLAN:18` |
| Q10 | **文档配额**：计划 ≤16 KiB、分析 ≤8 KiB（8 KiB = 8192 B）；**取代而非叠加** | **[推断]** | `engineering-rules.md` R30 + `handoff-payload-plan.md` §9.1 |

## 4. 未决/待用户裁决（U）—— 残片逐字抄录 [确定]

| 编号 | 问题 | 依据 | 选项 |
|---|---|---|---|
| U5 | `build/` 是否排除出 iCloud 同步 | 本会话冲突副本 538→1703→1411，已致 3 次构建失败 | 排除 / 不排除 |
| U6 | `docs/development/images/`（守则配图）是否纳入版本 | 目前 untracked | 纳入 / 忽略 |
| U7 | 经验条目对账（我写的 R1–R36 vs 守则）是否现在做 | 守则 §0.2 要求经验条目不得冲突 | 做 / 稍后 |

> U1–U4 原文不可恢复 **[缺失]**（编号已占用）。

## 5. 来源索引（残片逐字抄录 + 指针更新）

- 守则：`docs/development/软件工程守则.md`（最高思想）
- 流程条款与用户裁决副本：`AGENTS.md`（含「最高思想与质疑义务」「代码修改流程」「设计与规划审查岗 design-critic」「验证门槛」各节）
- 进度与冻结清单：`docs/plan/MASTER-PLAN.md`（**§4 执行顺序与依赖** · **§7 风险与技术债**）
- 活跃项与待裁决：`docs/plan/MASTER-PLAN.md`（**§1 目标与范围** · §2 阶段门）
- 本轮规划：`docs/plan/MASTER-PLAN.md` · 经验条目：`docs/development/engineering-rules.md`
- 架构自审查：`docs/analysis/top-down-architecture-20261007.md`
- 事故归档：`docs/analysis/incident-20261007-agents-requirements-revert.md`
