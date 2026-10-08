# Handoff / Payload 计划与设计（自包含 v1）

> 性质：L 级（wire 承载 + native 执行 + 攻击路径控制）。**自包含，不引用任何外部计划。**
> 状态：设计稿 v1。**所有未决项已由作者给出设计默认（§10），只需一次接受或推翻**；被推翻的历史提法见 [handoff-payload-decisions.md](handoff-payload-decisions.md)。
> **操作细则**：[handoff-payload-ops.md](handoff-payload-ops.md)（op 词表 / 参数来源 / gate 原因 / 控制器签名 / 破坏面）。
> **2026-10-07 裁决**：**导入无限制**（任何 profile 可携带 Lua 脚本；**L2 只展示不拦截**；**沙箱保留**）；最坏代价 = **panic**（可接受，需冷启）；**硬线 = 不得往磁盘乱写、尤其 AVB**（D29）；**脚本引擎 Lua 5.4**（实测 107 KB ≈ 现有二进制 2.2%，D22）；分发侧恶意脚本由**用户自检**，不在本工具职责内。
> 基线：HEAD 45d06516；工作树源码未改动。

## 1. 分层归属

| 层 | 放什么 | 不放什么 |
|---|---|---|
| **HOCON**（静态档） | 内核几何、执行调参、**available 按 backend 分组**（route/操作/seam/能力）；即**攻击流程配置**；**脚本来源与信任级别**（L1/L2，D27） | **handoff**（本次启动什么） |
| **ResolvedProfile**（会话级） | 用户意图展开后的**完整 plan**：操作序列、执行器、脚本/模块清单、哈希、快照 | — |
| **wire** | 该 plan（**含 plan 用到的 backend 集合**的段与几何） | 其它 backend |
| **native** | plan → 严格执行；gate 拒绝 | 运行期决策 |

## 2. 执行器结构

- 接口概念 ExecutorPolicy：kind / available(facts) / run(plan)；实现四个：**UserRoot、Umh、CredGrant、KernelCmdSet**。
- **执行器 token 在 document 里唯一确定**（构建期写死）；gate 只做**二值判定**：available 不成立 ⇒ 具名拒绝、**不许启动攻击**。
- 运行期按 token **直接 switch**（无虚表、无运行期注册）调用对应 policy。
- 不变式：**gate 不产生备选**；**run 不决策、不换路**。

## 3. 队列模型（攻击流程配置）

**细粒度操作**（取代粗粒度的 w1/w2/w3）：条目为 **{backend, op, route?, …}** 或 **{backend, seam, stage}**；**backend 轴并入队列**（与 terminal 轴同样由条目表达）。op 入口/写模式/重试来源见 [handoff-payload-ops.md](handoff-payload-ops.md) §1–§2，gate 原因见 §4。

    queue = [
      { backend = "cve_2026_43499", op = "write.selinux", route = "tcp_zerocopy" },
      { backend = "cve_2026_43499", seam = "plugin", stage = "init" },          # 结构预留；插件仍冻结
      { backend = "cve_2026_43499", op = "write.cred", route = "tcp_zerocopy" },
      { backend = "cve_2026_43499", op = "repair.scratch", route = "multicast_waiter" },
      { backend = "cve_2026_43499", op = "write.seccomp.flags", route = "select_stack" },
      { backend = "cve_2026_43499", op = "write.seccomp.mode", route = "select_stack", attempts = 2 },
      { backend = "cve_2026_43284", op = "write.pagecache" }      # 连续块（D18）
    ]

- **元素 = 扁平标量键**（R1 裁决）：`{backend, op, route?, seam?, stage?, always?, attempts?}`（**7 键**；`params_ref` **已删**）；**参数值不进队列**——参数走**两条既有通道**（见 [params](handoff-payload-params.md)）。**成员表权威见 [queue-schema](handoff-payload-queue-schema.md) §3**；**既有 manifest 行不变、payload 新键按新行补充**（R2）。
- **backend 可缺省**（D16）：条目未写 `backend` 时取 document 根 backend；写了即以条目为准。
- **跨 backend 的顺序/依赖显式声明**（D19）：未声明即拒绝（gate 校验）。
- **落盘归属（K-9）**：App 只产**字节 + sha256**；**落盘统一归 native**（攻击前写 `GHOSTLOCK_HOME` 固定路径 0600）；`.sh` 模板 = **合成在 App、落盘在 native**；**配置快照 / `HoconWriter` 仍在 Kotlin**。

- **do_something(route)**：每个操作**自带 route**，init/clean **隐式**（prepare→execute→disarm→destroy 由实现完成）——**与现行「每写一实例」行为一致**，无需改 route API。
- **seam 位置规则（纯静态）**：seam **只能声明在操作之间**，**操作内部在 schema 层不可表达**——取代「只许首/尾」。设计理由：操作边界处无存活 PI waiter——证据 `race/threads.cpp:61-65`（waiter 消失后才 `controller.execute()`）；`plugin/host.hpp:102` 只说明「`.so` 不得在 waiter 存活期映射」，**不足以单独支撑本命题**。
- **payload 列表**：exec.scripts=[{id, sha256, bytes}]（**无项数上限**；正文 >256 B 走 `bin`（≤64 KB），其余键拒 `bin`）；load.modules=[{path, sha256, require_bypass?}]（**上限 8**）；**`always` 是条目属性**（非脚本项属性，与 kotlin §3 一致）。
- **always** 是**条目属性**（无条件执行），不是 step 属性、不是独立步骤。
- 脚本三层收尾：**模板后段**（进程内收尾；用户脚本插前段、子 shell 隔离）→ **always 条目** → **内核兜底**（relay restore_enforce / watchdog）。

## 4. 控制器（取代三个过程式算法）

| 控制器 | 层 | 职责 | 不变式 |
|---|---|---|---|
| **HandoffPlanBuilder** | Kotlin | 意图 → plan：展开（唯一权威）、模板合成、**产出字节 + sha256**、配置快照 | 发射前 **plan 完备且钉死**，不留运行期决策 |
| **PlanGate** | native | plan × 参数 schema × 设备能力 → Supported / **Refused(具名)** | **只二值判定**，不生成备选 |
| **PayloadRunController** | native | 严格执行：逐条目、always 语义、后置条件核验、诊断 | **执行路径 ≡ document**；失败具名，不换路 |

纯函数工具（渲染 / 哈希 / 模板拼接）保持过程式，不进控制器。

## 5. 构建期收敛（禁止运行期回退）

- 执行器、route、脚本、模块路径与哈希**在构建期唯一确定**并写入 document；
- 能力不足 ⇒ **拒绝启动**（不是运行期改用别的办法）；
- 「回退」= **另一次运行 / 另一份 plan**；同一次攻击内**零分支**。

## 6. 失败语义

- 任何操作失败 ⇒ **结束**（不换 route、不换执行器）；**DirtyFailure ⇒ 终止进程**（沿用既有语义）。
- 全部失败**具名**；提权已发生的部分照记，结论标「未完成」。

## 7. 自由度与卡点

| 维度 | 判断 |
|---|---|
| 同 backend 内 **多 route** | **允许**：plan 用到的 route 集合，其几何随 document 一起发射 |
| 多 **backend** 同场（如 43499+43284） | **设计内支持**（backend 轴并入队列，条目自带 backend）。两个卡点与对策：① **状态槽**——`CoreSession.backend_state` 单实例 ⇒ **追加式第二槽**（偏移不变；`session_layout_test` **扩展**而非改；退路 = 同 backend 操作**相邻连续块**）；② **装配**——`Pipeline` 编译期单 backend ⇒ 按条目 backend **直接 switch**（无虚表） |
| 代价 | 操作 × route 的实例化数增加 ⇒ 机器码形状变化 ⇒ cmp 归因 + **真机门禁** |
| 脚本来源（**D27**）的代价/风险 | **残余风险 = 最坏 panic**（可接受，需**冷启**恢复）；**分发侧恶意脚本由用户自行严格检查**，不在本工具职责内；**硬线见 D29**（能力面不得含块设备/任意路径写 ⇒ 脚本无法触达 AVB/分区）。缓解 = L3 沙箱：不注册 `io/os/package/debug/load/dofile/require`，只暴露 op/probe/slot 白名单；`lua_sethook(LUA_MASKCOUNT)` 指令预算；`lua_setallocf` 内存上限；单脚本 **64 KB**；仅在**操作之间**运行；错误或超预算 ⇒ **立即终止**（不换路）。**引擎**：Lua 5.4（D22；实测 107 KB ≈ 2.2%） |

## 8. 判据（指针）

> **判据唯一权威** = [verification](handoff-payload-verification.md) §1–§4（黑盒/白盒 + 极端值 · 扩展点 · 技术债 · CBN）+ 各文档判据节：b0 §7、b1b2「判据」、queue-schema §4、plugin-lua §5。
> 原表已**收敛**：**seam 位置用例删除**（操作内部在 schema 层不可表达 ⇒ 不可达）；**细粒度等价**保留为**真机门禁**项。
> **命令**：`make -C src native-host-tests`；`./gradlew :app:testDebugUnitTest`；`./gradlew :profile-core:test --tests "*QueueElementShape*"`。**真机门禁**：触碰 `steps`/`primitives` 写路径时必须（归档 `device-gates/*.md`）。

## 9. 攻击流程（端到端）

    P0 攻击前（Kotlin）：意图 → plan → 模板合成/包装 → **只产字节 + sha256（不落盘）** → 发射 wire
    P1 启动前（native）：**落盘脚本到 GHOSTLOCK_HOME 固定路径（0600）** → PlanGate（参数 × 能力 × 顺序）→ 启动 / Refused 具名拒绝
    P2 执行（native）：按 plan 顺序跑操作；每操作 do_something(route)（隐式 init/clean）
                        seam 在操作边界按需插入（插件冻结期内为结构预留）
    P3 payload：load{modules[]} → exec{scripts[]}（always 条目无条件执行）
    P4 收尾：三层保证（模板后段 / always / 内核兜底）+ 后置条件核验

### 9.1 文档配额与增补策略

- **上限**：计划 ≤16 KiB、分析 ≤8 KiB（**8 KiB = 8192 B**；依据 `engineering-rules.md` **R30**，守则本身无 KB 配额）；**取代而非叠加**。
- **冻结增补（22:23 快照；以最新实测为准，集合不变）**：`b0`（0）· `queue-schema`（12）· `batch-plan`（19）· `plan`（22）· `decisions`（22）· `kotlin`（37）⇒ **只可替换/改指针**；`b1b2`（246）与 `ops` 不在列；新内容写 `-ops-words`/`-kotlin-io`/`-params`/`-verification` 或新建文件。
- **拆分点**：ops 再扩 ⇒ 拆 §1 词表到 `-ops-words.md`；decisions 再扩 ⇒ 拆 §4 沿革到 `-history.md`。
- **代码引用约定**见 [queue-schema](handoff-payload-queue-schema.md) 头部。
- **命名**：新文件固定 `handoff-payload-<topic>.md`（≤8 KiB）；**一处权威**。

## 10. 设计默认（**未决项已全部由此收敛，你只需接受或推翻**）

| # | 默认 |
|---|---|
| D1 | 队列顺序 = **前缀 + 尾追加**（w1..w3 之后是 load/exec 与细粒度操作） |
| D2 | 模块项上限 **8**；脚本项**不设上限** |
| D3 | **暂不允许**用户自备 .ko（只允许 relay 与 App 预置件） |
| D4 | **删除**显式 route init/clean 步骤（由操作自带） |
| D5 | 重试语义落到**操作属性 attempts**；链级 park/respawn 保留为「重试块」结构 |
| D6 | 执行器四选一，**构建期**决定；UMH 不可用时构建期选 CredGrant |
| D7 | 默认**恢复 SELinux enforcing**（安全默认） |
| D8 | 后置执行者 = **保活的用户态收尾进程**（+ 内核兜底） |
| D9 | exec 清单**进 wire**（可校验、可对拍） |
| D10 | seam 位置 = **纯静态判定**（只能声明在操作之间；操作内部不可表达）；插件运行时仍冻结，只预留结构 |
| D11 | 43499 **允许**不依赖 KernelSU 裸装 relay；失败即结束（另一次运行再选路径） |
| D12 | **不改签名闸**：若裸装被拒，构建期就选用户态 root 路径（不在运行期关闸） |
| D13 | KernelSU 不特殊化：它就是 load/exec 的一个条目（Kotlin 展开） |
| D14 | **操作词表（修正版）**：write.selinux · write.cred · **write.seccomp.flags + write.seccomp.mode（两枚背靠背）** · repair.scratch · probe.selinux · **probe.leaf（仅非 tcp）** · probe.seccomp · spawn.victim · park · handoff.root_child · payload script；**drain 不独立成 op**（重试块属性）。细则见 ops §1–§2 |
| ~~D15~~ | ~~多 backend 同场：设计上预留，本批不实现~~ → **已被 D16–D19 取代**（backend 并入队列，设计内支持）；编号留空作沿革 |
| D16 | 条目形态 = **{backend, op, route?, seam?, stage?, always?, attempts?}（7 键）**；~~`params_ref?`/`params?`/`seam={seam,id,stage}`~~ **已划掉**；**根 backend 可缺省**；细则见 [queue-schema](handoff-payload-queue-schema.md) |
| D17 | **document 发射 plan 用到的 backend 集合**（每个 backend 的段与几何），不再只发一个 backend |
| D18 | 状态槽**追加式第二槽**（既有成员偏移不变；`session_layout_test` 扩展而非改）；**本批不做则走退路**：同一 backend 的操作必须相邻的连续块 |
| D19 | **跨 backend 的顺序/依赖必须在 plan 里显式声明**，gate 校验；**未声明即拒绝** |
| D20 | **handoff 数据槽不进队列元素**（R1 = 7 键扁平 schema）：数据槽为 **op 级**声明（`OpSpec.produces/consumes`，b1b2 §B1.1）或由脚本 `slot.*` 承担；gate 静态校验，未声明即拒绝（见 queue-schema §6） |
| D21 | 条件表达式**受限文法**（固定产生式、无循环/递归、无 I/O）；**不引入通用脚本**（脚本层另见 D27） |
| D22 | **已采用 Lua 5.4.7 作为运行期脚本引擎**（**保留源码解析**；vendor 见 b1b2 §B2.1）。实测：核心 `.text = 109 532 B ≈ 107 KB`；仅字节码 86 KB；相对现有二进制 4.84 MB ≈ **2.2%**（详见 ADR-0008）。**结论：保留源码解析**；**不引入 luac**。**选型对照与依据**见 [ADR-0008](adr/0008-lua-runtime-sandbox.md) |
| D23 | D14 的正式表述：op 边界 = **一次 `do_something(route)`**；写类 op 必须可单独重试，**seccomp 两枚视为同一重试块** |
| D24 | `probe.leaf` 的 `attempts=4`/`settle_us=50000` 由硬编码（`steps.cpp:215`）**迁入 Owner Schema** |
| D25 | `geometry-missing` 由**静默回落**改为**具名拒绝**（复用 `MissingRequired` 诊断） |
| D26 | `plan_gate` 插入点 = `state_from` 之后、`Backend::run` 之前；orchestrator 的 `selection_supported`/`combination_available` **并入 gate 去重**（ops §5） |
| D27 | **脚本来源 = 三层信任模型（导入无限制）**：**L1 声明层**（backend/op/route/params/顺序/条件）**自由导入**（不可执行）；**L2 脚本层**：任何 profile 均可携带 Lua 脚本，**不设确认/签名门禁**——静态提取的「引用原语清单 + 能力需求 + 预算声明」与「作者/来源 + sha256」**仅作展示信息，不拦截**；**L3 沙箱硬边界保留并强化**（见 D29/D30）。**取代**旧默认「脚本只允许 App 生成、导入脚本一律拒绝」 |
| D28 | **约定/诊断（非门禁）**：鼓励 op 名用**字面量或声明常量表**（便于工具列出与展示）；**「禁止动态拼接」不作为导入门禁**，仅产诊断 |
| D29 | **硬线（能力面收窄）**：op 白名单内**不得存在块设备或任意路径文件写原语**，脚本在能力面上**无法触达 AVB/分区**；现有 op 中仅 `payload script` 的产物写**固定路径**（`GHOSTLOCK_HOME`，攻击前，由 App/native 控制）。**新增任何含文件或块设备副作用的 op，必须列入「受限副作用清单」并单独评审** |
| D30 | **类型化句柄**：脚本对 native 资源只能持 **`{kind, index, generation}`** 句柄（**有界 64**，见 b1b2 §B2.3），**不暴露裸指针/裸地址**；句柄由探针产生，使用时按 kind 校验 ⇒ 恶意脚本**无法伪造地址**去驱动写原语 |
| D31 | **插件控制面用 Lua**：与 payload 脚本**共用同一个 Lua 5.4 VM、同一套沙箱与预算、同一套 manifest 校验**；不注册 `debug/load/dofile/require/io/os/package`；**每插件一个 `_ENV` 沙箱 + 独立预算账户**（指令/内存分开计；单插件超限**不影响其他插件**）；**插件描述符 = 脚本返回的 manifest 表**（能力位、stage、params schema、版本、哈希）；宿主 API 面全体插件可达，**任何扩展必须过受限副作用清单（复用 D29）**。细则见 [handoff-plugin-lua.md](handoff-plugin-lua.md) |
| D32 | **解冻形态与保留通道**：插件**仍处于冻结状态**；解冻时按 **D31 形态**实现（**不再恢复旧 `.so` 单一插件路径**）；现有 **`.so` 通道 + 探针 + `kernel_channel` + host 生命周期**保留给**内核型插件**（内核热点 / 需直接内核交互的对策模块） |
