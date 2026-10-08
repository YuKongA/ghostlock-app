# Handoff / Payload 决策与沿革（对话整理）

> 配套 [handoff-payload-plan.md](handoff-payload-plan.md)。**被推翻的提法一律划掉保留**，不删历史。
> 自包含，不引用任何外部计划。

## 1. 已裁决（用户口径）

| 日期 | 裁决 |
|---|---|
| 2026-10-07 | **payload 不冻结**（撤销冻结）：与配置系统高度耦合的工程 |
| 2026-10-07 | **terminal 轴正式取消** |
| 2026-10-07 | **payload 装进 step**；用户**自由组合**；**不支持的组合不许启动攻击** |
| 2026-10-07 | **43499 也可以在取得 root 后加载中转 LKM** ⇒ 两 backend 统一路径 |
| 2026-10-07 | **用户态 root 保留为回退**，但**回退在构建期收敛** |
| 2026-10-07 | 不论用户态 root 还是 LKM，执行的必须是**同一份预制脚本** |
| 2026-10-07 | 参数：**传入脚本路径**；**攻击前构建** |
| 2026-10-07 | 后置步骤放**脚本模板**里：用户脚本插**前段**，**后段保留 cleanup** |
| 2026-10-07 | exec 用**列表**（exec: [...]），不写多个 exec 块 |
| 2026-10-07 | 脚本**不设数量上限**（可拼接） |
| 2026-10-07 | **禁止运行时回退**，**严格按 document 执行**；失败只结束 |
| 2026-10-07 | **handoff 只在 ResolvedProfile 及以后**；HOCON 放**攻击流程配置**（available.queue），不限制动态启动 |
| 2026-10-07 | 攻击流程配置**要拆分**（w1/w2/w3 太笼统）：queue 可声明**具体 route 操作**、**多 route 序列**、**预留 plugin 结构** |
| 2026-10-07 | 每写都要重建 ⇒ **init/clean 简化为 do_something(route) 自动完成** |
| 2026-10-07 | **backend 直接拆进 queue**（条目自带 backend）：现行形态 = **7 键扁平标量** `{backend, op, route, seam, stage, always, attempts}`；~~`{backend, op, route?}` / `{backend, seam, id, stage}`~~ **已划掉**（`id`/`params_ref` 已删）；**多 backend 同一 plan 内**（D16–D19） |
| 2026-10-07 | **自纠（Lead）**：D14 初版**漏 `probe.leaf`**、并把 `write.seccomp` **误合为一枚**；修正版 = plan §10 D14/D23 |
| 2026-10-07 | **允许导入脚本**（用户需要导入「特殊优化的攻击路径」）：**三层信任模型** = L1 声明自由导入 / L2 脚本知情确认 / L3 沙箱硬边界（D27/D28） |
| 2026-10-07 | **脚本引擎采用 Lua 5.4**（D22 关闭）：核心 `.text = 109 532 B ≈ 107 KB`（源码解析保留；仅字节码 86 KB）≈ 现有二进制 4.84 MB 的 **2.2%**；**不引入 luac** |
| 2026-10-07 | **导入无限制**（任何 profile 可携带 Lua 脚本，**L2 只展示不拦截**）+ **沙箱保留**；最坏 = **panic**（可接受，冷启）；**硬线：不得往磁盘乱写（尤其 AVB，见 D29）**；分发侧恶意脚本**用户自检** |
| 2026-10-07 | **插件控制面采用 Lua**（D31/D32）：与 payload 共用 VM/沙箱/预算与 manifest 校验；**插件仍冻结**，解冻按此形态（**不再恢复旧 `.so` 单一通道**） |
| 2026-10-07 | **审查原则 v0.2 生效**：**D1 取代** AGENTS「守卫/断言必须证明能失败」与 `engineering-rules.md` **R10** 的强制证伪要求（改为**黑盒/白盒 + 极端输入值**，不可能用例不测）；原条目**保留为可选手段**；C3 增「编译期收益须与工程成本权衡，成本超过灵活方案则让路」；F2 扩为**技术债预判**（记录见 `docs/development/design-review-principles.md`） |
| 2026-10-07 | **脚本/模块条目带 `sha256` provenance**（取代旧「payload 页无 hash 字段」）：导入无限制 + 用户自检需要可核对的来源与身份 |
| 2026-10-07 | 三个算法改由**专门控制器**承担 |
| 更早 | 三层一致；去 canonical 中间态；native 单一权威；**注册套件**（能力/后端/step 三接口 + 注册服务 + 专有参数进 document / ~~只加载**一个 backend** 的结构~~（被 D17 取代，见 archive §1）） |

## 2. 被推翻的提法（划掉保留）

**近一轮划掉项**（历史全表 21 条 + 自纠记录见 [handoff-payload-decisions-archive.md](handoff-payload-decisions-archive.md)）：

| 原提法 | 替代 | 理由 |
|---|---|---|
| ~~「脚本只允许 App 生成、导入脚本一律拒绝」~~ | **三层信任模型**（D27：L1 自由 / **L2 只展示不拦截** / L3 沙箱） | 用户需导入**特殊优化路径** |
| ~~L2 门禁「导入需首次或变更时用户确认」~~ | **无限制导入**：来源/sha256/原语清单**仅展示不拦截**；边界 = L3 沙箱 + D29/D30 | 用户裁决：导入无限制 |
| ~~「payload 页无 hash 字段」~~ | 脚本/模块条目**带 `sha256` provenance**（展示 + 落盘复核） | 导入无限制 + 用户自检需要 provenance |

## 3. 自纠记录（Lead）

历史自纠见 [handoff-payload-decisions-archive.md](handoff-payload-decisions-archive.md) §2（与划掉项同处，保留历史）。

## 4. 沿革与技术债

| 事项 | 代价 | 偿还时机 |
|---|---|---|
| payload 冻结 → 解冻 | 恢复/重写 wire 校验、App 入口、执行面；**真机门禁** | payload 批次收口 |
| terminal 轴取消 | ADR-0006、UML（状态机/类图/序列）、需求条目同批修订 | 与设计同批 |
| 双读取路径废止 | 冻结物重冻 + 对拍改单路径 | 待用户批准 |
| 细粒度操作拆分 | 操作 × route 实例化 ⇒ cmp 归因 + **真机门禁**；重试/park 语义需新家 | 实现批次 |
| **backend 并入 queue** | 状态槽追加（或退路＝同一 backend 连续块）+ 逐条**直接 switch** 分派（无虚表）+ 两链同进程的 **PI race/panic 面** | 实现批次 + **真机门禁** |
| `probe.leaf` 参数硬编码 + geometry 静默回落 | 参数不可配（steps.cpp:215）；缺几何时静默回落会掩盖配置错误 | 实现批次（**D24/D25**） |
| **导入脚本** | 导入配置不再是纯数据；缓解 = L3 沙箱 + **能力面收窄（D29/D30）** + **原语自带校验与 `verify`** | 实现批次 + **真机门禁** |
| 采用 **Lua 5.4**（D22） | 二进制 +107 KB（≈2.2%）；第三方源码需登记来源/许可并隔离告警 | 构建批次实测 + 许可归档 |
| **导入无限制**（安全责任转移） | 从「工具拦截」改为「**能力面收窄（D29/D30）+ 用户自检**」；硬线验证归实现批次 | 实现批次 + **真机门禁** |
| 插件形态：`.so` 单通道 → **Lua 控制面 + `.so`/LKM 内核型双层** | 新增沙箱/预算/描述符校验与解冻门禁；`.so` 通道保留为内核型保留通道 | 插件解冻批次 |
| **技术债预判（F2）汇总** | 六项（`OpRegistry` 可插拔 / 键上限顶格 / `slot.*` 持久化 / 沙箱 API / payload 解冻 / gate 原因码） | 见 [verification](handoff-payload-verification.md) §4 |
| **越权取代**：守卫证伪实验要求（AGENTS 验证门槛 / R10） | 代价 = **vacuous 断言风险回归**（删掉被保护的赋值仍可能全绿） | 偿还 = 由**黑盒/白盒 + 极端值用例**覆盖替代（原则 v0.2 D1） |
| **本轮评审修订（N/K/B 三类）** | **N/B0/B1/B2 面**：contract 只放中性类型（`WriteOutcome`）、不新造 `WritePath`（沿用 `RouteLifecycle`）、`StepExecution` → `OpSpec`/`OpRegistry`（fold 归 B1）；gate **10 条静态原因** + `{ok,reason,path,index}` + `main.cpp:184` 去重；`bin` 仅脚本键 + manifest/golden 重冻；层表登记 `pipeline`/`script`。**K 面**：新增 [handoff-payload-kotlin.md](handoff-payload-kotlin.md)（多 owner、queue 新 schema、bin 内联 presence-gated、gate 镜像、单一展开器） | 与 b0/b1b2/ops/kotlin 同批 |

## 5. 未决项已收敛为设计默认

原先散落的待定问题（顺序规则、模块上限、自备 ko、cleanup 归属、后置执行者、恢复默认、wire 清单、执行器选择、裸装、签名闸、KernelSU 定位、多 backend、**脚本引擎**、**插件形态**）**已全部收敛**，集中列在 [handoff-payload-plan.md](handoff-payload-plan.md) §10（**D1–D14、D16–D32**；**D15 已被 D16–D19 取代**）。
用户只需一次动作：**接受，或逐条推翻**。被推翻的条目按 §2 的格式划掉保留。
