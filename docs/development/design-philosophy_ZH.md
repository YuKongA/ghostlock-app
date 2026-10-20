[English](design-philosophy.md) | 简体中文

# GhostLock 设计原则

改动前先读本文。具体做法与验证门槛见 [工程规范](engineering-standards_ZH.md)。

## 1. 用真机证据确认设备支持

主机测试核对代码是否符合规格，真机测试确认设备上的实际结果，两者不能互相替代。没有设备证据的判断必须标为假设。新 profile 未过真机验证不得标 `supported`。

## 2. 显式表达契约

route 由 profile 显式选择，不从内核版本推断。缺省与显式 `0` 必须分开保存，GLK1 用键是否出现表达 presence。文件、线程和子进程的所有者、借用者与终结点必须能从类型或接口看出。

route 生命周期固定为 `prepare → execute → disarm → destroy`。必要的约定必须能指向类型、字段或测试，不能依赖口头共识。

## 3. 每项数据只保留一个权威

profile 是配置权威，执行层消费不可变 `TargetProfile`。`ResolvedAddresses` 发布后只读，Native 与 Kotlin 的注册表由一致性测试约束。全局状态只允许 session singleton 与启动期只读上界。

新增状态先检查能否归入已有对象，并写清所有者与生命周期。

## 4. 能在编译期检查的就提前检查

用类型、`constexpr` 和编译期检查拒绝非法组合，数据结构保持 host 可编译。攻击窗口内的抽象必须通过机器码对比证明没有额外成本，做不到就使用直接代码并说明原因。新代码零告警，clang-tidy 零 findings。

## 5. 只改本批次范围

改动前列出文件清单与明确保留项，清单之外冻结。`kernelsnitch/` 移植代码不顺手重写，legacy v1 与新路径隔离。重写必须先明确目标和验证方式，L 级改动先获认可。

大改动拆成可独立验证的批次，上一批验证并记录后再进入下一批。

## 6. 设计时就写验证方法

每个 L 级设计必须有验证矩阵，说明如何观察结果、比较基线和复现条件。设备记录包含 commit、设备、运行次数与日志名。失败记录与通过记录同等归档。

## 7. 报告不确定性

单次成功或失败不能证明规律。`KERNEL-PANIC-01` 的代码归因需要同构建复现、冷机复跑和栈证据。没有确认的结果写明未验证，不隐藏失败，也不夸大通过范围。

## 8. 校验边界并明确清理

profile 驱动的尺寸与偏移在使用前检查溢出和范围，验证失败不得发布 active profile。攻击窗口内不得引入异常、分配或间接调用。新增参数必须有边界检查和拒绝语义。

清理与 disarm 分开设计。无法证明可安全回收的状态保留到进程退出，`dirty` 是合法终态。不得为了看似干净而提前释放。

## 9. 保存决定和证据

改动前用 `git log --all -- <path>` 查已有设计。决定记录背景、决定、理由、备选和后果。计划写要做什么，日志写实际发生了什么，不能混在一起。

历史文档归档到 git 后仍须可检索。失败实验与成功记录同样保留，供后续改动核对。

## 10. 自动检查一致性

能机械检查的规则交给构建、测试或脚本，例如双侧字段一致性、clang-tidy 和攻击函数形状对比。新增一致性要求时，同时提供测试、脚本或清单。需要判断的部分交给非作者评审。

## 11. 对改动负责

- 先读代码与历史，再确定改动范围和保留项。
- 能验证的执行验证，不能验证的写明原因。
- 不夹带重构或追新，证明改动没有破坏既有行为。
- 把决定、结果和失败记录留给后续维护者。

## 附录：原则到做法的映射

| 思想（本文） | 做法（[`engineering-standards_ZH.md`](engineering-standards_ZH.md)） |
|---|---|
| §1 设备支持证据 | §8 验证与证据规范（门禁、归因纪律、V&V 分层） |
| §2 显式契约 | §3 数据流（presence/单一权威）、§2.2 route 显式选择、§6 命名与接口契约 |
| §3 单一权威 | §3.1 配置数据流、§2.4 全局状态、§5.1 wire 权威 |
| §4 编译期检查 | §2.3 编译边界、§5.2 类型设计、§8.2 cmp_disasm |
| §5 改动范围 | §1.4 批次化与明确保留、§2.5 上游/legacy |
| §6 验证设计 | §1.3 门槛、[`documentation-standards_ZH.md`](documentation-standards_ZH.md)（计划/门禁模板） |
| §7 不确定性 | §8.4 归因纪律、§1.5 提交规范 |
| §8 边界与清理 | §3.2 溢出与范围校验、§4.2 PI 窗口规则、§4.3 错误分层 |
| §9 决定与证据 | [`documentation-standards_ZH.md`](documentation-standards_ZH.md)（分类地图、ADR、归档、现状注） |
| §10 自动检查 | 双侧一致性测试、clang-tidy 策略、`cmp_disasm`、路由模板 |
| §11 改动责任 | §9 Agent 检查清单与禁止模式 |

## 参考

标准：SWEBOK v4.0、ISO/IEC/IEEE 12207、ISO/IEC 25010、IEEE 1012、ISO/IEC/IEEE 24765。
实践：[C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)、
[Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)、
[Google SRE Book Ch.15](https://sre.google/sre-book/postmortem-culture/)、
[Software Engineering at Google](https://github.com/laofo/SoftwareEngineeringAtGoogle)。
本地出处见 [`engineering-standards_ZH.md`](engineering-standards_ZH.md) 附录。
