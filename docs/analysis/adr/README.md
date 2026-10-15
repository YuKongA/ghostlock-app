# ADR 索引

> 本目录的架构决策记录（ADR）。编号连续；**0005 与 0007 不存在**（未使用/留空）。
> 每份 ADR 自包含；本索引只做**一览与定位**，不复述条文。新增 ADR 须同批在本表登记。

| 编号 | 标题 | 一句话结论 | 状态 |
|---|---|---|---|
| [0001](0001-top-level-architecture.md) | 顶级命名空间评估与漏洞原语模块化 | 顶层 namespace 按层收敛，漏洞原语模块化归属 backend | Proposed（待维护者确认） |
| [0002](0002-exploit-session-generalization.md) | ExploitSession 通用化 | session 与具体 CVE 解耦，成为跨 backend 的通用容器 | Proposed（待维护者确认） |
| [0003](0003-profile-schema-registration.md) | profile 注册模型（中性 document + owner schema） | 中性 document + owner schema 注册；运行期不建注册表、每 (section,key) 一个 owner | Proposed（待维护者确认） |
| [0004](0004-framework-convergence.md) | 框架收敛（第四轮审查裁决） | 组合轴收敛到稀疏 triple（R18–R21）；**不引入虚表**（概念 + 静态 policy） | Accepted（2026-10-03） |
| [0006](0006-terminal-ownership.md) | terminal 归属 | 实现下放 backend、**词汇保留**；共享件中性；装配权威在 `component_catalog` | Accepted（2026-10-05） |
| [0008](0008-lua-runtime-sandbox.md) | Lua 运行期与沙箱 | 采用 vendored Lua 5.4（源码解析，≈107 KB/2.2%）；白名单沙箱 + 预算/内存/64 KB；`bin` 仅脚本键；句柄 `{kind,index,generation}` | Proposed（待用户批准，2026-10-07） |
| [0009](0009-script-trust-and-hardline.md) | 脚本信任模型与能力面硬线 | 导入无限制（L2 只展示不拦截）；**硬线 = op 面无块设备/任意路径写**（D29）；句柄 D30；插件 Lua 控制面 D31/D32 | Proposed（待用户批准，2026-10-07） |
| [0010](0010-lua-cpp-resource-model.md) | Lua 与 C++ 资源模型（作用域 · 句柄生命周期 · GC 边界） | C++ 独占所有权、Lua 只持句柄；作用域三分类（窗口/op/脚本）；八条硬规则 + 五条具名拒绝；**句柄为 POD，无 `__gc` 释放路径** | Proposed（待用户批准，2026-10-08） |
| [0011](0011-lua-execution-order-authority.md) | 执行顺序的权威归 Lua | 执行顺序由 Lua 决定；native 只提供步骤与动作 | Accepted（用户裁定 2026-10-09） |
| [0012](0012-capability-authorities.md) | 接口声明能力与 backend 提供能力各有一处权威文件 | 每层能力各有一处权威，不许第二处复述 | Accepted（用户裁定 2026-10-09，v2） |
| [0013](0013-kotlin-legacy-cross-checks.md) | Kotlin 侧为旧架构，跨端对拍降级 | 跨端对拍不再作为门槛；设计不得迁就旧架构 | Accepted（用户裁定 2026-10-09） |
| [0014](0014-kernel-memory-only-surface.md) | 对外只暴露 kernel memory；上层步骤上移为 Lua 工具函数 | 一个虚基类覆盖所有 backend 能力；只暴露 kernel memory | Accepted（用户裁定 2026-10-09） |
| [0014 附一](0014-kernel-access-refinements.md) | KernelAccess 的两批细化（逐条落地） | 附件：能力状态、参数、工厂等逐条落实 | 附件（随 0014） |
| [0014 附二](0014-kernel-memory-conflict.md) | 冲突与两条路（虚基类 vs 既有机器判据） | 附件：记录冲突与选择依据 | 附件（随 0014） |
| [0015](0015-profile-through-lua.md) | profile 解析结果一律经 Lua 中转 | native 不直接把 profile 转到执行层 | Accepted（用户裁定 2026-10-09） |
| [0016](0016-script-argument-crossing.md) | 脚本参数如何过界（Str 一类的归约） | Str 归约为固定表内一项，查不到即具名拒绝 | Proposed（待用户批准） |
| [0017](0017-jcs-canonical-form.md) | GLKv3 规范化与摘要（JCS/RFC 8785 · sha256 · base64 载荷） | 记契约声明 + **实现只校验 digest 形式**的技术债 | Proposed（待用户批准） |

## 相关但与本文档无关的索引

- 需求编号与占用：`docs/development/requirements.md`（F/I/Q/P 各段）。
- 全流程结构图（IPO/状态机/Class/Sequence）：`docs/development/full-process-uml.md`。
