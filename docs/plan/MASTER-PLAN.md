# MASTER-PLAN：GhostLock 主计划（唯一进度入口）

## §0 依据与权威

- **权威顺序**：`docs/development/软件工程守则.md`（最高思想）> `AGENTS.md` > `engineering-standards.md` / `design-philosophy.md` / `engineering-rules.md` > `docs/analysis/**`。
- **审查依据**：`docs/development/design-review-principles.md`（**v0.2**：D1 判据形式 = 黑盒/白盒 + 极端输入值；C2 扩展点；C3 成本权衡；F2 技术债预判）。
- **策划框架**（守则 §5.1）：**输入** = 《用户需求报告》（`docs/development/requirements.md`）；**输出** = ① 软件开发计划书（**本文件**）② 质量保证计划（`design-review-principles.md` + `engineering-rules.md`）③ 配置管理计划（`AGENTS.md` 文档约定 + 本文件 §8）④ 里程碑及评审计划（本文件 §5）。
- **生命周期模型**（守则 §2.1）：**增量 + 阶段门**（每个批次一类事，上一批验收全绿再进下一批），阶段划分 = 立项 → 需求 → 概要设计 → 详细设计 → 编码 → 测试 → 运行/维护。
- **需求基线状态**：`F21`–`F29` / `I13` / `I14` 已恢复（[确定]）；**`F1`–`F20`、`Q1`–`Q9` 原文在 2026-10-07 事故中丢失**（`requirements.md` 标 [缺失]，见 [incident](../analysis/incident-20261007-agents-requirements-revert.md)），**待用户补写后再据此批准**。
- **唯一性声明**：本文件是**唯一进度入口**；`docs/plan/**` 其余 28 份已归档到 `docs/archive/20261007-2237-*`（索引见 §11 与 [INDEX-plans](../archive/INDEX-plans.md)）。

## §1 目标与范围

**目标**：把 payload/handoff 运行时、配置模型、插件形态三块收敛为可执行、可验收的批次序列：
1. **payload/handoff 运行时**：contract 化（中性 `WriteOutcome`）→ 原语注册 + gate（10 条静态原因）→ Lua 5.4 运行期（沙箱/预算/句柄/bin 承载）。
2. **配置模型收敛**：三层同形（`ResolvedProfile` 单一权威读入口）+ 注册套件（catalog 编译期投影）+ 校验去重（registry 族 `F21`–`F24`/`I9`–`I11`）。
3. **插件形态**：解冻后按 Lua 控制面（D31）+ `.so`/LKM 内核型保留通道（D32）。

**不在范围**：不改 `schema == 3` 与 canonical 字节；不引入运行期注册表/虚表；不动 520B/784B 与 `CoreSession` 槽位；不恢复旧 `.so` 单一插件通道；不新增 CLI/旁路（`I6`/`R13`）。

## §2 生命周期与阶段门

| 阶段（守则 §2.1） | 本工程活动 | 产物 | 门 |
|---|---|---|---|
| 探索 | 读代码/文档 + 历史门禁；只读取证（`file:line`） | 分析文档（`docs/analysis/**`） | — |
| 需求 | 需求编号与占用（`F21`–`F29`/`I9`–`I14`） | `requirements.md` | — |
| 概要设计 | 三层同形 + 注册套件 + gate 结构 | `handoff-payload-plan.md` / `resolved-profile-registry-plan.md` | **G0 设计批准** |
| 详细设计 | b0/b1b2/queue-schema/kotlin/kotlin-io/params/plugin-lua/verification | 同上 + ADR-0008/0009 | G0 |
| 编码 | 批次 B0.1 → B0.2 → B0.3 → B1 → B2 → B3 | 代码 + 测试 | **G1 批次验收** |
| 测试 | 宿主/native 构建/真机三层判据 | 测试记录 + XML 三桶计数 | **G2 真机门禁** |
| 运行/维护 | 归档、沿革、技术债偿还 | `docs/archive/**` + 本计划 §7 | **G3 归档** |

- **G0**：**design-critic 非作者评审总判 = 可进批准 gate（已过）**；**用户批准 = 待记**（**ADR-0008/0009 状态 = Proposed（待用户批准）**；`requirements.md` 重建后**仅存编号与含义（无状态列）**；批准后**同批回填时间与出处**）。**不得回填未发生的批准**。
- **G1**：批次验收判据全绿（§5 命令），阻断项为零。
- **G2**：攻击路径改动 = **真机门禁唯一判据**（冷机、固定 CPU 对、单 route、KernelSU 未加载；归档 `docs/analysis/device-gates/*.md`）。
- **G3**：批次收口时同批更新 UML、归档门禁记录、登记技术债。
- **裁剪声明（守则 §2.1）**：**无立项/合同阶段**（自研工具、无外部合同）；**测试 = 宿主 / native 构建 / 真机三层**；**退役不在范围**；需求以 `requirements.md` 编号承接，不另立需求规格书。

## §3 工作分解（WBS）

| 工作包 | 目标 | 设计依据 | 写范围（主要） | 验收判据（黑盒/白盒 + 极端值） | 门禁 |
|---|---|---|---|---|---|
| **WP-1 设计收敛** | 三族设计定稿并可评审 | `../analysis/handoff-payload-*.md`（plan/b0/b1b2/queue-schema/kotlin/kotlin-io/params/params-dispatch/plugin-lua/verification）、`../analysis/resolved-profile-registry-plan.md` | 仅文档 | design-critic 总判 = 可进批准 gate；外部引用 0；配额内 | G0 |
| **WP-2 contract 化与层归属** | 中性 `WriteOutcome`；`pipeline`/`script` 层登记 | `handoff-payload-b0-contract.md` §3/§4 | `contract/write_primitive.hpp`、层表、`Capabilities` → POD | 层边界 = 0；窗口 TU 禁 include（文件级扫描）；概念 `static_assert` | G1（**不动机器码**） |
| **WP-3 条目 route 选路** | 条目值 → `switch` → 编译期策略 | `b0` §8（B0.3）、`params-dispatch` §4 | `route_policy.hpp` 调用侧 | 白盒：`op_tu_isolation_test`；`cmp` 无间接 call | **G1 + G2（动机器码）** |
| **WP-4 原语注册 + gate** | `OpSpec`/`OpRegistry` 取代 `StepExecution`；`plan_gate` 10 条 | `b1b2` §B1.1/§B1.2、`ops` §4 | `pipeline/registry.hpp`、`contract/registry_check.hpp` | 极端输入逐条具名（含 `executor-unavailable`）；两处结论冲突 ⇒ 失败（`main.cpp:184`） | G1 |
| **WP-5 Lua 运行期** | vendored Lua 5.4 + 沙箱 + 预算 + 句柄 + `bin` | `b1b2` §B2、`ADR-0008`、`kotlin-io` §1–§2 | `src/lib/lua`、`script/` 绑定层、`glkv3.hpp` 上限 | 超预算/越权 API/伪造句柄/非脚本键 `bin`/插入段 >64 KB ⇒ 均拒；成员表两侧机器对拍 | G1（+G2 若动写路径） |
| **WP-6 插件 Lua 形态** | 解冻后：`_ENV` 沙箱 + 独立预算 + manifest 表 | `handoff-plugin-lua.md`、`ADR-0009`、`plan` D31/D32 | 插件宿主与 `script/` | 单插件超限仅自身失败；未知能力位/非法 stage ⇒ 加载期拒；A 读 B 的 `_ENV` ⇒ 不可达 | 解冻门禁（G1+G2+G3） |
| **WP-7 配置模型收敛** | 三层同形 + 单一读入口 + 注册套件 | `resolved-profile-registry-plan.md` §3–§6 | 按该计划 §4 改动清单 | 见该计划 §6 验证矩阵（3 层同形/路径唯一/行数守恒/单一读入口） | G1（B2 触攻击路径 ⇒ G2） |
| **WP-8 文档与度量** | 配额、同批动作、归档、度量 | 本文件 §5/§8/§9/§10、`batch-plan` §4 | `docs/**` | 外部计划引用 0；配额内；同批清单逐项落地 | G3 |

> **判据集（不复制）**：[verification](../analysis/handoff-payload-verification.md) §1 总则 · §2 样例 · §3 扩展点 · §4 技术债 · §5 CBN；本表只列极端输入摘要。

## §4 执行顺序与依赖

```mermaid
flowchart LR
  W1["WP-1 设计收敛（G0 已过）"] --> B01["B0.1 contract 概念+层表"]
  B01 --> B02["B0.2 能力层接线/隔离（不动机器码）"] --> B03["B0.3 条目 route 选路（cmp+真机）"]
  B03 --> B1["B1 原语注册 + gate（10 条）"] --> B2["B2 Lua 运行期（沙箱/预算/句柄/bin）"]
  B2 --> B3["B3 插件 Lua（解冻后）"]
  W1 --> R["WP-7 配置模型收敛（独立族，F21–F24/I9–I11）"]
  R -. "验收触发（非写依赖）" .-> B2
```

- **串行纪律**：同一时刻**只允许一条写入流**（`AGENTS.md`）；批次顺序 **B0.1 → B0.2 → B0.3 → B1 → B2 → B3**，上一批验收未绿不得进下一批。
- **并行约束（M2 更正）**：**写入恒为单流**（同一时刻只有一个 writer 改被门禁覆盖的文件）；**并行仅限只读勘察与门禁**，且**共享 `build/**` 与容器缓存的门禁必须串行**。WP-7（registry 族）不得与 WP-2…WP-5 并行写入。
- **依赖**：WP-4 依赖 WP-2（层归属）；WP-5 依赖 WP-4（registry/ParamSpec）；WP-6 依赖 WP-5（同一 VM/沙箱）；WP-3 必须先于 WP-4（选路是 gate 后执行面的前提）。**WP-7 = 独立族**：其验收在 **B2 批触发**（`../analysis/resolved-profile-registry-plan.md` §6 矩阵）；图中 R ⟶ B2 为**触发边**，非写依赖。

## §5 里程碑与门禁

| 里程碑 | 命令（全部需 EXIT=0，失败数读 XML 全量） | 真机 |
|---|---|---|
| B0.1 / B0.2 | `make -C src native-host-tests`；`make -C src ghostlock`（NDK 零告警）；`make -C src lint-tidy` | 否 |
| B0.3 | 同上 + `tools/cmp_disasm.py build/native/ghostlock-B0 build/native/ghostlock`（**可选诊断**） | **是**（动写路径） |
| B1 | `make -C src native-host-tests`；`./gradlew :app:testDebugUnitTest` | 否 |
| B2 | 同上 + `./gradlew :profile-core:test --tests "*QueueElementShape*"` + `make -C src profile-manifest-v3`（**既有 110 行逐字节不变；payload 新键按新行补充**） | 若动写路径 ⇒ 是 |
| B3 | 插件解冻门禁（`AGENTS.md` 恢复清单 + 真机 + `docs/analysis/device-gates/*.md` 归档） | **是** |
| WP-7 | 按 `resolved-profile-registry-plan.md` §6 矩阵（`make -C src native-host-tests`、`./gradlew :app:testDebugUnitTest`、5 张 manifest 目标） | B2 批触发 |

## §6 角色与职责

| 角色 | 写范围 | 职责 |
|---|---|---|
| **Lead** | 全局 | 裁决、批次门禁、提交纪律、冲突合并、用户沟通 |
| **native-core** | `src/core/**` | native 设计/实现/门禁（host/NDK/lint/真机）、`file:line` 取证 |
| **kotlin-app** | `profile-core/**`、`app/**`、assets | Kotlin/配置/UI 设计实现与对拍；不碰 native |
| **docs-uml** | `docs/**` | UML/计划/ADR/门禁归档/配额；**唯一文档写者** |
| **design-critic** | **只读** | 非作者评审（`AGENTS.md` 岗位节）：冻结快照 + 固定输出格式 + 总判 |

- 受影响的组（守则 §5.1）：软件工程组 = Lead + 三个常驻流；测试/质量保证 = 门禁命令 + design-critic；配置管理 = docs-uml；文档支持 = docs-uml。

## §7 风险与技术债登记

| # | 风险/债务 | 影响 | 处置 |
|---|---|---|---|
| R-1 | **ADR-0001（20190 B）/ 0003（8512 B）/ 0004（16624 B）超限** | 证据文档超「分析类 ≤8 KiB」 | 独立待办：0001 findings 移交 `architecture-findings-register.md`；0003 压缩 stale 段；0004 拆正文 + 附录 |
| R-2 | **R1 层表未登记 `pipeline`/`script`** | 层约束无机器守卫 | B0.1 验收项（`include_firewall_test.cpp` 层表 + 空账本） |
| R-3 | **判据命令可执行性** | 文档判据与真实目标名不一致会失效 | 每批在 CI/本地跑 §5 命令并记录退出码 |
| R-4 | **payload 白名单触点** | 解冻后 `backend.<id>.payload.*` 会被 fail-closed 拒 | 解冻批次为两个 `Backend*Keys` 增 `payload` + 对拍（`kotlin.md:63`） |
| R-5 | **vacuous 断言风险回归** | 删掉被保护的赋值仍可能全绿 | 由**黑盒/白盒 + 极端值用例**覆盖（原则 v0.2 D1） |
| R-6 | **PI 窗口时序** | 窗口内分派/分配/首次触碰新页会引入不确定性 | E4 约束 + `cmp` 归因 + 真机门禁（`route_lifecycle.hpp:9-11`） |
| R-8 | **代码注释悬空计划路径**（审查时实测 **10 处**） | 注释指向**从未存在**的 `docs/analysis/<plan>.md` | owner = **native-core**（`src/core` + `tools/extract_rs`）/ **kotlin-app**（Kotlin）；**时机 = 已随本批交付完成**（见 [INDEX-plans](../archive/INDEX-plans.md) §六 末尾「悬空引用」行）；判据 = 编译通过 + `grep` 无失效引用（**现测 0**） |
| R-7 | **技术债预判六项**（`OpRegistry` 可插拔 / 键上限顶格 / `slot.*` 持久化 / 沙箱 API 扩展 / payload 解冻 / gate 原因码） | 未来扩展成本 | 逐项偿还时机见 `../analysis/handoff-payload-verification.md` §4 |

## §8 配置管理与变更控制

- **沿革**：被推翻的提法**划掉保留** + 一行理由；用户裁决逐条记录（`../analysis/handoff-payload-decisions.md` 与 archive）。
- **变更控制**：设计者必须**逐条回应**审查（接受并改 / 举证反驳），不得沉默忽略；强制确认的越权须同时有**沿革 + 技术债**登记。
- **批次纪律**：`engineering-rules.md` **R22/R14**（一批次一类事；判据分层留记录）；未列入改动清单的代码即冻结。
- **文档配额与增补**：计划 ≤16 KiB、分析 ≤8 KiB（**8 KiB = 8192 B**，`R30`）；**取代而非叠加**；增补策略见 `../analysis/handoff-payload-plan.md` §9.1（冻结清单 + 拆分点 + 命名）。
- **同批动作**：UML 逐图/ADR/需求/测试的对应关系见 `../analysis/handoff-payload-batch-plan.md` §1–§5。

## §9 估计（守则 §5.2 / §5.5(5) / §5.6）

| 估计项 | 值 | 依据 / 口径 |
|---|---|---|
| **规模** | **8 个工作包**（WP-1…WP-8）· **6 个批次**（B0.1→B3）· 设计文档 15 份（家族）+ 3 份 ADR | 守则 §5.5(5)「工作产品规模」；实测 `wc` |
| **工作量** | 以**批次**为单位推进；每批**一类事**，门禁次数 ≥3/批（host / NDK 或 Gradle / lint 或对拍）；真机门禁仅 B0.3 与触及 `steps`/`primitives` 写路径者 | 守则 §5.5(5) 时间表口径 |
| **成本** | **≈ 0**：自研工具、无合同；仅现有工作站 + 一台测试设备（电/时间） | 守则 §5.6「资源估计」（本项目不适用合同/外包） |
| **资源** | 现有工作站（macOS + NDK 30）+ 一台测试设备（真机门禁）+ 容器引擎（LKM 批） | `AGENTS.md` 常用命令 |
| **可度量替代** | 需改文件数上限 = **≤5 文件/WP**（超限 ⇒ 拆包）；门禁次数/批 ≥3；审查阻断关闭 = 0 未闭 | 本计划 §3/§5 与 §10 |

- **说明**：无 CMMI 级测量库 ⇒ 不给「人日」估计（守则 §5.2 说明估计误差与能力成熟度相关），改用**可数计数**（包/批/门禁/文件数）作为可核验替代。

## §10 度量与状态

| 度量 | 当前值（**截至本次更新**；取证快照以 §11 归档与 §7 登记为准） | 目标 |
|---|---|---|
| 设计状态 | **评审已过（可进批准 gate），待用户批准**；阻断 **0** | 批准后回填 |
| 代码改动 | **0**（本轮仅文档） | 按批次推进 |
| 审查阻断关闭 | **已全部关闭**（评审结论与沿革见 [decisions](../analysis/handoff-payload-decisions.md)、三族文档与 `docs/archive/20261007-2237-branch-plan.md`） | 0 未闭 |
| 待办计数 | ADR 超限拆分 3 项 · R1 层表登记 1 项 · payload 白名单触点 1 项 · 技术债 6 项 | 按 §7 偿还 |
| 文档配额 | 家族 15 份 + ADR-0008 全部在上限内；[archive README](../archive/README.md)、[INDEX-device-gates](../archive/INDEX-device-gates.md)、[INDEX-plans](../archive/INDEX-plans.md) 各 ≤8 KiB | 保持 |

## §11 归档索引

- **本次归档（28 件）**：`docs/plan/<原名>` → `docs/archive/20261007-2237-<原名>`（头部含原始路径 / 归档原因 / 归档日期）；规则见 [archive README](../archive/README.md)，**计划类索引**见 [INDEX-plans](../archive/INDEX-plans.md)。
- **归档规则指针**：命名 = `YYYYMMDD-HHMM-<原文件名>`；每件头部含原始路径、归档原因、归档日期；历史不删（`AGENTS.md` 文档约定）。
- **`docs/analysis/**` 遗留计划**：**候选为空**——名含 plan 的仅 `handoff-payload-plan.md`、`handoff-payload-batch-plan.md`、`resolved-profile-registry-plan.md`，**全部为保留的设计依据**；**owner = Lead**（决定前不动）。规则与索引见 [archive README](../archive/README.md) §五、[INDEX-plans](../archive/INDEX-plans.md)。
