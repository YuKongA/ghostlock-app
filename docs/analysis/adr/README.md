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

## 相关但与本文档无关的索引

- 需求编号与占用：`docs/development/requirements.md`（F/I/Q/P 各段）。
- 全流程结构图（IPO/状态机/Class/Sequence）：`docs/development/full-process-uml.md`。
