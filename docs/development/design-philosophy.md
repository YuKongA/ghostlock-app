# GhostLock 设计哲学（Design Philosophy）

> 本文的每条原则都以**软件工程理论与公认实践**为主要依据（标准、经典文献、工业实践，见附录 A），
> 本项目的经验只作为该理论在特定约束下的**实例化证据**。
> 分层：哲学（本文，为什么）→ 规范（`engineering-standards.md`，怎么做）→ 操作（`AGENTS.md`）。
> 效力：任何改动开始前必须读过本文。

---

## 0. 理论框架：本项目采用哪一套软件工程

我们不发明方法论，只做**裁剪（tailoring）与实例化**：

| 理论框架 | 对项目的含义 |
|---|---|
| **SWEBOK v4.0**（IEEE CS, 2024）的 "generally accepted knowledge" | 采用其知识域划分（Requirements / Architecture / Design / Construction / Testing / Maintenance / Configuration Management / Quality / Operations / Professional Practice）组织工程活动；"一般接受"不等于处处套用，**团队有责任对具体项目裁剪** |
| **ISO/IEC/IEEE 12207:2026**（软件生命周期过程） | 以**过程（process）**而非阶段组织工作；过程可重入、可组合；阶段以**决策门（decision gate）**结束——本项目的"门禁/证据归档"就是 decision gate 的实例 |
| **ISO/IEC 25010**（产品质量模型） | 明确优先质量属性：**reliability**（fault tolerance / recoverability）、**maintainability**（modularity / analysability / testability）、**security**、**performance efficiency**；本次序也反映取舍优先级 |
| **ISO/IEC/IEEE 24765**（SE 词汇）与 IEEE 1012（V&V） | 术语与活动定义的权威来源（verification ≠ validation，见 §1、§6） |

**本项目的约束放大并收紧了理论**——内核利用代码具有四条现实约束（§1–§8 由此展开）：

- **不可观测**：错误发生即重启，栈日志是唯一证词；
- **不可迭代**：同一构建可 PASS/panic/PASS，相关性容易伪装成因果；
- **代价不对称**：一次越界是设备损坏，不是"测试失败"；
- **知识易失**：指令序列的微小变化会改变时序，进而改变成败。

在常规项目里这些是"最佳实践"；在这里它们是**必要生存条件**。以下 11 条原则各自对应一组理论与一条约束。

---

## 1. 实证主义：真机是唯一裁判

**理论依据**
- **Empirical / Evidence-based Software Engineering**（Kitchenham 等系统评价方法）：技术主张的有效性必须由实验、案例与重复验证支撑，而不是由权威或直觉支撑。
- **V&V（IEEE 1012；ISO/IEC/IEEE 12207）**：*Verification* 回答"是否按规格构建（building it right）"；
  *Validation* 回答"是否构建了正确的东西（building the right thing）"。主机测试是 verification，
  真机门禁才是 validation——**两者不可互相替代**（Boehm, *Software Engineering Economics*, 1981）。

**原则**：主机测试、静态分析、代码推理都只是假设；真机日志才是判决。

**项目实例化**：Xperia 首攻 V1–V21 的每个假设（promoted child、`empty_zero_page`、BSS scratch、
slide-only、超时缩短）都被真机逐一否决或确认；TCP/Select 因缺少设备只称"仅主机固定测试"。

**行为要求**：没有设备证据的结论称为假设，不称为结论；未验证路径不得标 `supported`。

## 2. 信息隐藏与契约：显式优于隐式

**理论依据**
- **Information hiding（Parnas, CACM 1972）**：模块边界应沿"**最可能变化的设计决策**"划分，
  用稳定的接口把变化隔离在模块内部——而不是按执行步骤划分。
- **Design by Contract（Meyer, 1988）**：接口的前置/后置条件与不变量应显式声明，而非隐含约定。
- **ISO/IEC 25010 maintainability**：analysability / modifiability 直接取决于依赖是否显式。
- **Google C++ Style Guide**：为读者优化——所有权转移等关键语义要留在调用点（"leave a trace"）。

**原则**：能写出来的东西，不要留给推断；隐式约定是不可审计的因果关系。

**项目实例化**：route 由 profile 显式选择而非从版本推断；"缺省"与"显式 0"用 presence mask 分离；
fd/pid/线程的所有权写进类型（`UniqueFd`/`VictimContext`/`PthreadOwner`）；生命周期四段
（`prepare → execute → disarm → destroy`）;配置只有一条注入路径（环境变量收敛）。

**行为要求**：任何"大家都知道"的约定，都必须能指向一行类型、一个字段或一个测试。

## 3. 配置管理与基线：每个事实只有一个权威

**理论依据**
- **Configuration Management（SWEBOK SCM KA；ISO/IEC/IEEE 12207 technical management 过程）**：
  配置项必须被**唯一标识**、变更受控、状态被记录、配置被审计；基线一旦发布即受控。
- **单一真实来源（SSOT）**：重复的权威不是冗余，而是延迟爆发的冲突。

**原则**：同一件事只有一个权威；双权威不会立刻出错，会在最难查的地方出错。

**项目实例化**：profile（GLK1/HOCON）是唯一配置权威，执行层只消费不可变 `TargetProfile` 快照；
`ResolvedAddresses` 发布后只读；Native↔Kotlin 双侧注册表由测试对拍锁定；全局状态收敛为一个 session
singleton + 一个启动期只读上界。

**行为要求**：新增状态前先问"它能否归属已有权威"；不能，才新增并写清所有者与生命周期。

## 4. 缺陷成本与左移：能在编译期决定的，不留给运行期

**理论依据**
- **Boehm（1981）的缺陷成本结论**：缺陷发现得越晚，修复成本越高（随生命周期阶段递增）；
  这是"设计评审、静态检查、编译期约束"等 **shift-left** 实践的经济学根据。
- **C++ Core Guidelines**：P.4 静态类型安全、P.5 编译期检查优先于运行期检查、P.8 不泄漏资源。
- **形式化方法的定位（Wing, *A Specifier's Introduction to Formal Methods*, 1990）**：
  并非全盘形式化，而是把可机械验证的部分尽量前移。

**原则**：编译期检查是免费且无法绕过的；运行期检查在攻击窗口里既昂贵又危险。

**项目实例化**：Route 能力是 `constexpr`，PI 窗口内没有一次间接调用；数据结构 host 可编译以便穷举测试；
C++ 现代化以 `cmp_disasm` 8 函数逐指令验证"抽象零开销"——把理论的成本论证变成可检验的命题。

**行为要求**：抽象要付得起；付不起就写直接代码并说明原因。新代码零告警、clang-tidy 0 findings。

## 5. 增量交付与变更控制：未列入改动清单的代码即冻结

**理论依据**
- **增量/迭代与螺旋模型（Boehm, 1986）**：以小的、可验证的增量推进，每一轮都重新评估风险。
- **变更控制（SWEBOK CM / ISO 12207）**：变更必须有申请、影响分析、批准与记录；
  **scope 之外即冻结**，否则失败无法归因。
- **Brooks, *The Mythical Man-Month*（1975）**：概念完整性（conceptual integrity）比功能堆砌更重要；
  第二系统效应警告"顺手重写"的代价。
- **受控重写（Henderson, *Software Engineering at Google*, 2017）**：定期重写能清除累积复杂性、传递知识
  与所有权；但重写必须是**有立项的工程**（目标明确、批次推进、逐批验证），不能以"顺手"形式夹带。

**原则**：一个批次只做一类事；未列入清单的代码不是你的战场。

**项目实例化**：每份计划都有"明确保留"清单；`kernelsnitch/` 上游代码不重写；legacy v1 与新路径隔离；
批次验证通过才进入下一批（S01–S15、CPP00–CPP17 均按此推进）。

**行为要求**：改动前先声明"我不碰什么"；大改动拆批次，每批独立验证并记录；
**要重写就先立项**——受控重写允许，顺手重写禁止。

## 6. 验证与确认设计：为验证而设计，证据优于断言

**理论依据**
- **IEEE 1012 / ISO/IEC/IEEE 12207**：V&V 是贯穿生命周期的独立过程，**验证策略必须在设计阶段确定**，
  而不是实现完后补测。
- **ISO/IEC 25010 testability**：可测试性是设计属性（可观测、可控制、可隔离），不是事后工具问题。
- **可追溯性（SWEBOK Requirements KA）**：需求 → 设计 → 测试 → 证据应可双向追溯。

**原则**：设计里没有"怎么证明它对"这一节，设计就是不完整的。

**项目实例化**：可观测（route 结果与计数写进日志）、可对比（8 函数形状对比给出 IDENTICAL/具名差异）、
可复现（冷机、固定 CPU、单 route、KernelSU 干净启动）、可追溯（门禁记录含 commit/设备/计数/日志名）。

**行为要求**：每个 L 级设计必须含验证矩阵；失败记录与通过记录同等归档。

## 7. 无指责复盘与经验主义：诚实面对不确定性

**理论依据**
- **Google SRE 的无指责复盘（blameless postmortem）**：复盘的目标是理解系统性原因并落地预防动作，
  而不是追责；隐藏失败才是最大的风险（*SRE Book*, Ch.15，源于医疗与航空业）。
- **经验主义（Agile/Scrum 的透明—检视—适应）**：决策基于观察到的现实，而非计划中的假设。
- **Empirical SE 的重复与置信度**：单次结果不构成规律；报告的强度要与证据强度匹配。

**原则**：不知道就说不知道；单次结果不构成规律。

**项目实例化**：`KERNEL-PANIC-01` 的判定纪律（同构建复现 + 冷机复跑 + 栈证据才允许归因代码）；
通过标准保守（重复运行、记录次数）；未验证路径明确标注，不靠模糊措辞掩盖。

**行为要求**：不夸大、不隐藏失败；用证据强度约束结论强度。

## 8. 可靠性工程与防御式编程：防御性地写内核

**理论依据**
- **ISO/IEC 25010 reliability**：fault tolerance（故障时维持功能）与 recoverability（故障后恢复）是
  独立质量子特性；"不崩"不是"能恢复"。
- **Defensive programming + Design by Contract**：在信任边界检查前置条件；对不可信输入早拒绝。
- **风险管理（ISO 12207 technical management）**：风险驱动地分配设计与验证精力——
  本项目把风险最高处（内核 ABI、攻击时序）配以最高门槛。

**原则**：先想最坏情况，再写正常路径；不确定的状态宁可保留到进程退出。

**项目实例化**：profile 驱动的尺寸/偏移全部做溢出安全与使用前范围校验；验证不过即拒绝发布 active
profile；攻击窗口内无异常/分配/间接调用；"dirty" 是显式可接受的终态，假装的干净更危险。

**行为要求**：新增 profile 字段或攻击路径参数必须带边界检查与拒绝语义；清理与 disarm 分开设计。

## 9. 知识管理与决策记录：可检索的记忆

**理论依据**
- **Knowledge management / Information management（ISO 12207 organizational / technical management 过程）**：
  知识是需要被管理的过程资产，包括来源、版本与可检索性。
- **ADR（Nygard, 2011）**：每个架构决策记录背景、决策、理由、备选与后果——决策日志比结论更有价值。
- **软件考古学（software archaeology）**：历史是理解现状的最快路径。

**原则**：你今天的推理，明天就是别人的证据。

**项目实例化**：计划与日志分离（要做什么 vs 发生了什么）；门禁证据链（S/CPP/U01/NS*）；历史文档从
工作树归档但完整保留在 git；失败实验（V1–V21）与成功记录同等详细。

**行为要求**：改动前先考古（`git log --all -- <path>`）；任何"选了 A 不选 B"的决定留下五要素记录。

## 10. 自动化与过程控制：机制防错，而非记忆防错

**理论依据**
- **Continuous Integration / 持续交付（Fowler；Humble & Farley, 2010）**：把质量规则编码为流水线门，
  让反馈自动化、确定化。
- **静态分析与程序分析（SWEBOK Quality KA）**：可机械检查的规则不应依赖人工注意力。
- **Fagan 评审（IBM Systems Journal, 1976）**：对无法自动化的部分保留结构化人工检查（代码评审）。
- **防错设计（poka-yoke）**：让错误无法发生，而不是要求人更小心。

**原则**：一条只能靠人记住的规则，等于没有规则。

**项目实例化**：双侧一致性由两个测试对拍；形状对比脚本化；零告警与 clang-tidy
`--warnings-as-errors` 写进构建；提交格式（Conventional Commits）与门禁模板降低评审成本。

**行为要求**：新增一致性要求时，同时提供测试/脚本/清单，让它不依赖记忆。

## 11. 职业实践：对后来者（包括 agent）的期望

**理论依据**
- **SWEBOK Professional Practice KA**：职业性表现为胜任、客观、保密、诚实报告与持续改进。
- **ACM/IEEE-CS《Software Engineering Code of Ethics and Professional Practice》（1999）**：
  以公众安全与诚实为首要义务；报告真实结果，不隐瞒缺陷。
- **同行评审（peer review / inspection）**：结论应经得起独立复核。

**原则**：先理解再设计、先证据再结论、保守而谦逊、留下痕迹。

**行为要求（四条）**
1. **先理解，再设计**：读代码与历史；写下改动清单和"不碰什么"；L 级改动设计先获认可。
2. **先证据，再结论**：能跑的都跑；不能跑的标"未验证"；不写"应该没问题"。
3. **保守而谦逊**：不炫技、不顺手、不追新；越是聪明的改动，越要证明它没有改变别的东西。
4. **留下痕迹**：决策、验证、失败都写进仓库——那就是下一个人的基线。

---

## 附录 A：软件工程理论依据（标准 / 文献 / 实践）

**国际标准**
| 来源 | 采用内容 |
|---|---|
| ISO/IEC/IEEE 12207:2026《系统与软件工程—软件生命周期过程》 | process/stage 区分、决策门、technical processes（requirements→design→implementation→integration→verification→validation→maintenance）、technical management 过程（decision/risk/configuration/information management）、knowledge management |
| SWEBOK v4.0（IEEE CS, 2024） | 知识域框架与 "generally accepted knowledge" 的裁剪观；Quality / Maintenance / SCM / Professional Practice KA |
| ISO/IEC 25010:2023（产品质量模型） | reliability（fault tolerance/recoverability）、maintainability（modularity/analysability/testability/modifiability）、security、performance efficiency |
| IEEE 1012（V&V） | verification 与 validation 的独立过程定位；验证策略在设计阶段确定 |
| ISO/IEC/IEEE 24765 | 术语权威（verification/validation 定义） |

**经典文献**
| 来源 | 采用内容 | 对应原则 |
|---|---|---|
| Parnas, *On the Criteria To Be Used in Decomposing Systems into Modules*, CACM 1972 | 信息隐藏：按最可能变化的决策划分模块 | §2 |
| Meyer, *Object-Oriented Software Construction*, 1988 | Design by Contract：前置/后置条件与不变量显式 | §2、§8 |
| Boehm, *Software Engineering Economics*, 1981 | 缺陷成本随阶段递增（shift-left 依据）；"building the right thing" | §1、§4 |
| Boehm, *A Spiral Model of Software Development and Enhancement*, 1986 | 增量迭代、风险驱动 | §5 |
| Brooks, *The Mythical Man-Month*, 1975 | 概念完整性；第二系统效应（克制重写） | §5 |
| Wing, *A Specifier's Introduction to Formal Methods*, IEEE Computer, 1990 | 形式化检查的适用范围 | §4 |
| Fagan, *Design and Code Inspections…*, IBM Systems Journal, 1976 | 结构化人工评审 | §10 |
| Nygard, *Documenting Architecture Decisions*, 2011 | 架构决策记录（ADR）要素 | §9 |
| Kitchenham 等, 系统评价与实证方法（2004–2009） | 证据驱动的技术选择与重复验证 | §1、§7 |

**工业实践**
| 来源 | 采用内容 | 对应原则 |
|---|---|---|
| Google SRE Book, Ch.15 *Postmortem Culture: Learning from Failure*（CC BY-NC-ND 4.0） | 无指责复盘、失败即学习、复盘必须被评审与归档 | §7 |
| Google C++ Style Guide | 为读者优化、与既有代码一致、静态/全局对象的约束、规则必须"值得" | §2、§4 |
| C++ Core Guidelines（Stroustrup/Sutter） | P.4/P.5/P.8/P.10/P.11（静态安全、编译期优先、RAII、不可变、封装混乱构造） | §2、§4、§8 |
| Conventional Commits 1.0.0 | 变更意图机器可读，支撑变更管理与审计 | §3、§10 |
| Humble & Farley, *Continuous Delivery*, 2010；Fowler CI | 质量门自动化 | §10 |
| Henderson, *Software Engineering at Google*, 2017（[中文校订版](https://github.com/laofo/SoftwareEngineeringAtGoogle)） | 代码审查（至少一名非作者、小变更、开发中评审）、测试随变更强制、事故后剖析（影响/根因/行动项、不追责）、受控重写 | §5、§7、§10 |
| Anthropic《Claude Code best practices》 | agent 的 Explore→Plan→Implement→Verify、可执行验证闭环、证据优于断言、spec 先行 | §1、§6、§11 |

## 附录 B：原则 → 做法映射（操作入口）

| 思想（本文） | 做法（`engineering-standards.md`） |
|---|---|
| §1 真机是唯一裁判 | §8 验证与证据规范（门禁、归因纪律、V&V 分层） |
| §2 显式优于隐式 | §3 数据流（presence/单一权威）、§2.2 route 显式选择、§6 命名与接口契约 |
| §3 单一权威 | §3.1 配置数据流、§2.4 全局状态、§5.1 wire 权威 |
| §4 编译期决定 | §2.3 编译边界、§5.2 类型设计、§8.2 cmp_disasm |
| §5 冻结边界 | §1.4 批次化与明确保留、§2.5 上游/legacy |
| §6 为验证设计 | §1.3 门槛；`documentation-standards.md`（计划/门禁模板） |
| §7 诚实面对不确定性 | §8.4 归因纪律、§1.5 提交规范 |
| §8 防御性内核 | §3.2 溢出与范围校验、§4.2 PI 窗口规则、§4.3 错误分层 |
| §9 可检索记忆 | `documentation-standards.md`（分类地图、ADR、归档、现状注） |
| §10 机制防错 | 双侧一致性测试、clang-tidy 策略、`cmp_disasm`、路由模板 |
| §11 职业实践 | §9 Agent 检查清单与禁止模式 |

## 附录 C：项目实例证据（出处）

| 主题 | 出处（git 历史可检索） |
|---|---|
| 解耦设计与验收 | `DECOUPLING_PLAN.md` / `DECOUPLING_LOG.md`（commit `432a05f` 等） |
| C++ 迁移批次与门禁 | `docs/analysis/native-cpp-migration-plan.md`、`docs/development/native-modernization-plan.md` |
| 组件扩展清单与双侧一致性 | `docs/development/adding-a-component.md`（现行） |
| 全局状态审计 | `docs/analysis/native-global-state.md` |
| 入口/CLI 契约 | `docs/analysis/native-entrypoint-plan.md` |
| 环境变量收敛 | `docs/analysis/environment-convergence-plan.md` |
| 门禁证据链 | `docs/analysis/device-gates/**`（S04–S15、CPP00–CPP17、U01、NS*、PROFILE-*） |
| 首攻实验史（含否定结果） | `repro/xperia-first-success/README.md`（V1–V21） |
| 警告/clang-tidy 策略 | `docs/analysis/native-warning-audit.md`、`src/.clang-tidy` 注释 |
