# 事故归档：AGENTS.md 回退 + requirements.md 丢失（2026-10-07）

> 分析类（≤8 KiB）。本文件是**本次事故的唯一权威归档**；早先的 `agents-revert-incident.md` 已改为指向本文件的指针。依据：守则记录义务 + `AGENTS.md`「最高思想与质疑义务」。

## 1. 时间线（America/Toronto，2026-10-07）

| 时刻 | 事件 |
|---|---|
| 22:44 前后 | docs-uml 在 task-67 末轮，用 `tools.read(offset/limit)` 读**窗口**后把窗口内容当全文 `write` 回写 ⇒ **5 份文件被截断**：AGENTS.md 2227 B（原 46994）、requirements.md 966 B（原 15273/14244，**未跟踪**）、architecture-review-log.md 693 B、device-gates/43284-logging-20261005-pass.md 335 B、device-gates/s4-r2-20261005-pass.md 247 B。 |
| 22:44 | 发现后执行 `git show HEAD:<path> > <path>`（4 份 git 跟踪文件）⇒ **工作树未提交改动被 HEAD 版覆盖**（等效回退）。 |
| 22:45+ | Lead 察觉 `git status` clean，发布**禁令**：不得执行 `git checkout/restore/stash/reset/clean` 等改变共享工作树的命令。 |
| 22:5x–23:xx | task-70 恢复 AGENTS.md（逐节）、重做 3 份文件的今日指针行；task-71 重建 requirements.md + 建本归档 + 落两条硬规则。 |

## 2. 影响面

| 对象 | 影响 | 现状 |
|---|---|---|
| `AGENTS.md` | 今日全部未提交改动丢失 | **已恢复**（46994 B）：最高思想节 · 代码修改流程节 · design-critic 岗位节 · 守卫条目沿革行 · 构建产物必须在仓库内 `build/` 条目 · LKM 现状 · 共享缓存串行 · MASTER-PLAN/archive 指针 |
| `docs/development/requirements.md` | **原文不可恢复** | **重建**（6094 B）：[确定] 16 · [推断] 5 · [缺失] 5（F1–F20、I1–I8、Q1–Q4/Q6–Q9、U1–U4 只留编号行）；**需核对补全** |
| `architecture-review-log.md`、两份 device-gates | 仅丢今日 1 行指针 | 已重做 |
| 代码（src/profile-core/app/tools） | **未受影响** | — |

## 3. 根因（两个连续错误，责任人 = docs-uml）

1. **窗口读 + 全文写回**：`read(offset/limit)` 只给窗口，`write` 整体替换 ⇒ 截断。
2. **用 git 输出覆盖工作树**：`git show HEAD:path > path` 属**等效回退**，抹掉未提交改动。**非环境问题，非他人操作。**

## 4. 取证结果

- `git log --all -- docs/development/requirements.md` ⇒ **空**（从未提交）；`git show HEAD:docs/development/requirements.md` ⇒ 无此路径。
- Lead 取证：**6 个悬挂 blob 无命中**；**无副本**；**本机 TM 本地快照时点更早**（且快照不可挂载：`mount_apfs` ⇒ Operation not permitted）。
- 残片：`/tmp/forensics-task67/requirements.md`（966 B 尾部：U5–U7 + §8 来源索引）；另 4 份截断版同目录。
- 截断前 AGENTS.md 全文快照（本会话系统提示）⇒ 用于逐节恢复。

## 5. 恢复动作

1. 同目录 `/tmp/forensics-task67/` 留证（未删）。
2. AGENTS.md **逐节恢复**（不重写全文）：插入 3 节 + 修正 7 处条目 + 复指针。
3. 3 份被回退文件**重做今日指针行**（archive 路径 / `INDEX-plans.md`）。
4. requirements.md **重建并标注确定性**（本归档 §2）。
5. 两条硬规则落入 `AGENTS.md` 子智能体委派节 + `design-review-principles.md` **E 组**（见 §6）。

## 6. 两条新硬规则（已落地）

1. **禁一切改变共享工作树的 git 命令与等效写回**：不得执行 `git checkout/restore/stash/reset/clean`，也不得以 `git show REV:path > path` 等重定向写回工作树；历史比较只用**只读**形式（`git show REV:path`（不重定向）、`git diff REV -- path`、`git log`）。
2. **禁分片读 + 全文写回**：读必须整份读（或定位 `edit`）；若确需整份写回，**写回前后必须核对字节数与行数并写进报告**。

## 7. 遗留风险

- `requirements.md` 的 **[缺失]** 项（F1–F20 · I1–I8 · Q1–Q4/Q6–Q9 · U1–U4）**内容永久丢失**，只能由用户/Lead 重新表述；在补全前，凡引用这些编号的文档须注明「编号已占用、条文缺失」。
- 本事故说明：**未跟踪文件没有版本保护**；建议对被引用的关键文档**至少一次提交**（由 Lead 决定提交时机）。
