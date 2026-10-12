# Profile Lua 中转与纯 C++ backend 计划（2026-10-09）

## 现状与基线

- 分支基线：`a9db2fd4`；工作区已有未提交的设备 pipeline 批次，本计划不覆盖这些改动。
- ADR-0011 已接受：执行顺序的最终权威是 Lua。
- ADR-0014 已接受：执行层只暴露 kernel memory 能力，上层步骤移到 Lua 工具函数。
- ADR-0015 已接受：profile 解析结果必须经 Lua 中转，native 不直接把 profile 传到执行层。
- ADR-0008/0010 为 Proposed：沙箱、预算、句柄和资源边界需在本计划中落地。
- 当前仍是 Kotlin HOCON → GLKv3 → native `Document`；本稿只做 L 级迁移设计。

## 目标与约束

目标是固定四层：

```mermaid
flowchart LR
    K["Kotlin\n导入/选择/生成"] --> W["GLKv3\n脚本字节与摘要"]
    W --> H["C++ Lua Host\n校验/装载/边界"]
    H --> C["profile.lua\n只返回配置表"]
    C --> B["C++ typed binder"]
    B --> P["plan.lua\n受限 surface 编排"]
    P --> G["staged plan + gate"]
    G --> R["纯 C++ backend"]
```

硬约束：

1. backend、route、memory、terminal 不 include Lua，不依赖 Lua 类型或 VM。
2. Kotlin 不直接调用 Lua；Kotlin 与 native 仍以 GLKv3 为唯一进程边界。
3. `profile.lua` 与 `plan.lua` 使用不同 Lua 环境；profile 环境不能调用 surface，plan 环境不能修改 profile。
4. 两个脚本均由 C++ host 在窗口外执行一次；PI 窗口内不运行 Lua、不分配、不间接分派。
5. profile 值到达 backend 前必须经过 C++ 强类型绑定、范围检查和缺失检查；Lua 不能绕过拒绝语义。
6. 脚本字节、大小、摘要和 schema 仍是 Document 的受校验输入；不新增 wire 版本。

非目标：本批不实现新的攻击 primitive、不改变 kernel access ABI、不恢复插件/payload 运行时、不保留 HOCON 与 Lua 两个并列运行时权威。

## 目标形态

### 1. `profile.lua`：数据模块

profile 文件只返回一个有界表，不提供执行能力：

允许类型只有 `nil`（仅作缺省读取结果）、布尔、整数、短字符串和有限深度的表。禁止函数、userdata、线程、元表、闭包、全局写入、循环依赖和动态键生成。host 只按 manifest 声明的固定路径读取，不遍历表推导配置。

`profile.lua` 的结果立即复制为 C++ 不可变 snapshot；Lua GC 不持有 backend 资源。

### 2. `plan.lua`：行为模块

plan 文件只能访问冻结的 `cfg` 和 `script_surface`：

字段集合以 profile manifest 和 surface manifest 为准。plan 产生 staged plan，过 gate 后由 C++ backend 执行；gate 后不得继续运行。

### 3. 内置 loader

“另一个 Lua 模块负责加载”只能是**受 C++ host 控制的内置 loader**，不能是 profile 自带的 `require` 链：

- C++ host 注入脚本字节，不开放 `io`、`os`、`package`、`require`、`loadfile`、`dofile`、`debug`。
- loader 只负责 `loadbuffer → pcall → 形状检查 → binder`。
- profile loader 与 plan loader 各自拥有独立的 `_ENV` 和预算。
- loader 不能从文件系统自行寻找 profile；文件路径只由 Kotlin/host 选定并封装进 Document。
- 任意运行期错误、超预算、未知字段、错误返回形状均立即具名拒绝。

## 数据流与控制流差异

旧链路：

```text
HOCON → Kotlin ValueMap → NativeProfileDocument → GLKv3 → native Document → 执行层
```

目标链路：

```text
HOCON/导入值 → Kotlin 生成 profile.lua + plan.lua
             → GLKv3（脚本 bytes/size/sha256）
             → C++ host 校验
             → profile loader → typed snapshot
             → plan loader(cfg) → staged plan → gate
             → C++ backend library
```

唯一执行决策路径：

```text
profile bytes → profile.lua → C++ binder → cfg snapshot → plan.lua → staged plan → backend
```

backend 不再从 `profile::Document`、HOCON 或环境变量读取偏移和调参。未从 Lua 传入的必需参数不回退到 profile 或默认值，直接具名拒绝。

## 组件与文件改动清单（实现批次）

| 文件/目录 | 改动 | 理由 |
|---|---|---|
| `core/src/script/profile_loader.*` | profile Lua 装载、形状检查、预算、错误码 | 隔离数据脚本 |
| `core/src/script/plan_loader.*` | plan 装载、只读 cfg、staged 输出 | 落地 ADR-0011/0015 |
| `core/src/script/lua_host.*` | VM、allocator、hook、白名单、环境隔离 | 单一沙箱装配点 |
| `core/src/profile/*` | 保留 GLKv3 framing/摘要/大小校验；Document 增加脚本段视图 | wire 仍是跨进程边界 |
| `core/src/contract/profile_manifest.*` | 声明返回表的路径、类型、范围 | binder 的字段权威 |
| `core/src/backend/**` | 改为接收 typed snapshot/原语参数，删除 profile/Lua 依赖 | 纯 backend library |
| `profile-core/**` | HOCON/legacy 输入生成 profile.lua 与 plan.lua | Kotlin 仍负责输入与选择 |
| `app/src/main/assets/plan/**` | 保存 plan Lua 资产；profile Lua 按设备生成或随 Document 传输 | 计划与设备数据分开 |
| `tools/extract_rs/**` | 输出 profile.lua 数据模块或 typed model | 不生成行为脚本 |
| `docs/development/full-process-uml.md` | 更新唯一权威图 | 防止架构文档漂移 |

顺序：

1. B-L0：只实现 host、profile loader、typed binder 和固定表测试；backend 不改。
2. B-L1：接入 plan loader 与只读 `cfg`，保留现有 C++ backend 的测试替身。
3. B-L2：用一个 host fixture 从 HOCON 生成双 Lua，完成 GLKv3 对拍。
4. B-L3：迁移一个已接线 backend；触及攻击路径时进入真机门禁。
5. B-L4：迁移其余 profile，确认旧 HOCON 只剩导入/迁移用途后再归档旧运行时路径。

## 兼容性与回滚

- 不新增 `schema` 版本；脚本段是 GLKv3 schema 3 的新字段，需同步 manifest 和 golden。
- 迁移期间同一字段不能有两个生产读取者。
- 回滚点是批次边界：在 B-L3 前保留旧 native entry；B-L3 之后若真机失败，回滚到上一个已通过门禁的批次，不保留双读取旁路。
- 旧 HOCON 资产可作为导入输入保留，但不能继续作为 native 执行期的第二权威。

## 验证矩阵

| 批次 | 验证 | 期望 |
|---|---|---|
| B-L0 | profile loader：未知/缺失/错误类型/边界/函数返回/超预算 | 具名拒绝；合法表逐值绑定 |
| B-L0 | plan loader：surface 外名字、cfg 写入、伪造句柄、脚本错误 | 具名拒绝；cfg 只读 |
| B-L1 | 双 VM 隔离测试：profile 调 surface、plan 改 profile、跨环境引用 | 全部不可达 |
| B-L1 | staged 两次逐字节对拍、gate 前后行为对拍 | 相同输入得到相同 staged；gate 后不再执行 Lua |
| B-L2 | Kotlin → GLKv3 → native fixture；脚本 size/hash/schema/manifest 对拍 | 两份 manifest 与 golden 逐字节一致 |
| B-L3 | `make -C core host`、`make -C core check`、Android 构建 | EXIT=0、零新增告警 |
| B-L3+ | 冷机、固定 CPU、单 route、KernelSU 未加载的真机门禁 | 只认设备日志；失败与通过均归档 |

反证用例：删 binder 校验、profile 调 surface、plan 在 gate 后执行、backend 读取旧 Document；每项都必须使测试变红。

## 明确保留

- GLKv3 的 framing、canonical 编码、schema 数字和跨进程边界。
- `script_surface` 的最小能力集合、窗口内零 Lua、句柄不含裸地址。
- C++ backend 的 RAII、route 生命周期和真机判据。
- HOCON 作为导入/迁移输入，直到资产迁移完成；不再是 native 执行期权威。
- 当前未提交的 pipeline 临时批次；本计划不替其收口、不删除探针。

## 待用户批准的决策

1. 是否接受 `profile.lua` 作为 native 执行期 profile 的唯一权威。
2. 是否接受 GLKv3 在 schema 3 中增加 profile/plan 脚本段及其 manifest 行。
3. 是否接受 HOCON 降级为导入/迁移输入，而不是执行期第二权威。
4. 是否按 B-L0 → B-L4 批次推进，并在 B-L3 重新执行真机门禁。

获批前不改代码、不迁移资产、不删 HOCON 读取路径。

## 用户裁定与 `procedure` 契约（2026-10-10 · 待批准）

**用户裁定（原话）**：「全 lua 配置文件：创建一个 `procedure` 基类在**内置胶水层**，**外部 lua 配置文件需要实现 `procedure`**。」

### 落点（对本计划 §目标形态 的修订）

- 原「`profile.lua`（数据）+ `plan.lua`（行为）」收敛为：**外部配置 ＝ 实现 `procedure` 的 Lua 文件**；数据可经只读 `cfg` 传入（来源见下方待裁 6）。
- **内置胶水层**（随 native 分发、可信）提供三件：① `procedure` 基类（原型表 + `__index` 链 + 必需成员清单）；② 受限 surface 绑定（`writeop` / `copyop` / `settle` / `handoff` / `cfg` / `close`，权威见 `contract/script_surface.hpp`）；③ loader（装载 → 形状检查 → 预算 → 具名拒绝，ADR-0009 硬线）。
- **硬约束不变**（沿用 §明确保留）：**窗口内零 Lua**；句柄不含裸地址；`backend` / `route` / `memory` / `terminal` 不 include Lua。

### `procedure` 必需成员（草案，待批）

| 成员 | 形态 | 说明 |
| --- | --- | --- |
| `name` | string | 具名，用于日志与拒绝信息 |
| `steps` 或 `run(ctx)` | 数组 / 函数 | **顺序权威**（ADR-0011）；二选一，由 R10 定 |
| `on_success` / `on_failure` | 函数（可选） | 终态钩子；与 `terminal::FinishMoment` 对应 |
| `terminal` + `payload_ref` | string | handoff 必须同时给（script_surface A2：`handoff = {handle, terminal, payload_ref}`） |
| 只读 `cfg` | table | 数据视图；**写入即拒** |

### loader 的检查（全部具名拒绝）

1. 外部配置是否实现 `procedure`（缺成员 / 类型不符 / 出现 surface 外名字）；
2. 是否只引用受限 surface 的名字；
3. 运行期是否由 Lua 编排（**2026-10-10 裁定：是——限制靠运行时**，本条推翻此前的「gate 后不再执行 Lua」）；
4. 预算（指令数 / 内存）是否超限。

### 数据 × 行为如何组装（**已被推翻** · 2026-10-10 用户裁定）

> 裁定：**运行期仍由 Lua 编排，限制靠 Lua 运行时**（不新写步骤编排器）⇒ 本节「录制期装配、运行期无 Lua」的写法作废，保留为沿革。
> 现行结论见 [config-wire-redesign-plan.md](config-wire-redesign-plan.md)（§0/§4；版本号仍是 3）。

问题：若采档 1（行为全 Lua、数据留 profile/wire），两者在哪里相遇？答案是**在录制期相遇，运行期没有 Lua**。
这与本仓既有的「gate 后不再执行 Lua」一致，也决定了失败策略与数据分支必须**在录制期就显式写进 plan**。

| 阶段 | 谁在跑 | 数据形态 | 行为形态 | 产物 |
| --- | --- | --- | --- | --- |
| ① 装载期 | 宿主 + procedure | **只读 cfg 快照**（白名单键：设备事实/几何/预算/路由选择/名字表） | procedure 被装载、形状检查、具名拒绝 | 一个受限环境里的 procedure |
| ② 录制期 | procedure（一次性） | 读 cfg 做**有限分支**（如内核次版本、路由可用性） | 调 surface（writeop/copyop/settle/handoff/cfg/close） | **staged plan**：只有名字、序号、角色，无地址、无密钥 |
| ③ 准入 | 宿主 | — | stage_gate → plan_gate：可用性/几何/能力/顺序 | 可执行的 plan（或具名拒绝） |
| ④ 运行 | 执行层（**无 Lua**） | 名字 → 文档事实；角色 → 会话帧密钥 | 按 plan 顺序驱动 KernelAccess | 结果 + 失败策略（录制期已定） |

**三条铁律**

1. **密钥与地址不经 Lua**：plan 里只出现「角色」（如 session/credential 引用），解析点唯一（执行层）；SA/SPI/端口走会话帧、用后清零（序 6③）。
2. **顺序只在一处**：procedure 给出顺序（steps 或 run 的调用序），gate 只**拒绝**不改序；「换序即换行为」判据保持不变（ADR-0011）。
3. **失败策略在录制期决定**：on_failure 落成 plan 里的显式分支（abort / 跳到某条 / 收尾），因为运行期没有人解释 Lua。

**C++ 复杂度约束（用户 2026-10-10 裁定）**：引入 Lua **不得让 C++ 更复杂**。落地为四条设计承诺：

1. **只落在已有的边界上**：Lua 对接的必须是已经存在的面（`contract/script_surface.hpp`、`contract/op_registry.hpp`、`KernelAccess` 的参数表）——不新增适配层、不新增数据类型、不新增配置读取器。
2. **只能调用 C++ 已有的能力**：缺什么就**具名拒绝**，绝不因为「Lua 想表达」而给 C++ 加能力。
3. **除一个装载点外，C++ 不知道 Lua 存在**：`backend/`、`contract/`、`terminal/` 里不出现任何 Lua 类型；沙箱是叶子，只往已有接口里推。
4. **被取代的路径同批删除**：配置读取、按路由分支、顺序表、计划构造——Lua 接管的同时这些必须从 C++ 消失，否则 Lua 只是又加了一层。

**判据**：同一个行为，如果 Lua 版需要 C++ 新增任何东西才能表达，那么这个行为**不放 Lua**（或直接拒绝），而不是去扩 C++。
**装配面就是三张既有的名字表**：contract/script_surface.hpp（能调什么）、script/argument_names.hpp（名字↔数字，miss 即具名拒绝）、contract/op_registry.hpp（算子与参数）。
cfg 快照与 plan 的**对拍面**（键白名单 + 条目数/预算）由 manifest 声明，跨端列表不手抄。

**先例对照**（见 [script-layer-precedents.md](script-layer-precedents.md)）：Factorio 的只读 settings 全局=档①；Minecraft 的数据与代码靠**命名空间 id** 互相指名=档②；Fabric 由 loader **构造并注入**对象、mod 不读文件=档②③之间；NSE 的 script-args 参数化=档①；k8s 的 spec 与 controller 以「声明-收敛」相连、调用方不写步骤=档③④。

### 待裁（并入 §待用户批准的决策）

> **决策输入**：同类需求的既有设计调查（含闭源游戏 mod 生态、攻击框架模块契约、沙箱化配置语言）见
> [script-layer-precedents.md](script-layer-precedents.md)：三轴拆解 + 三档形态与代价 + 逐条对 5–7 的输入。
> 摘要：成熟生态几乎都「数据与行为分阶段」、能力按阶段/相位裁剪、可信度分级（WoW taint）、
> 确定性要设计进去；并提示**别把 depends 变成排序权威**（顺序归计划，ADR-0011）。

5. **形态**：`steps` 数组式 vs `run(ctx)` 函数式——决定编译期绑定失去后的补偿方式；
6. **数据归属**：`cfg` 来自 GLKv3 脚本段，还是与 `procedure` 同文件（「全 Lua 配置文件」的字面）——两者对 manifest 对拍面的要求不同；**已并入** [config-wire-redesign-plan.md](config-wire-redesign-plan.md) §3（全 Lua 源 + 生成物）；
7. **迁移边界三问（自 ROADMAP §4 第 5 条并入，避免两处待裁面不齐）**：① 哪些留 C++（内核原语面 `KernelAccess`／`OpRegistry`／include 防火墙／manifest 对拍／终端 `fork`+`payload_store`）；② 编译期绑定失去后由什么补偿（运行期门禁／manifest 对拍）；③ 门禁如何迁移（`make -C core check`、`plan-check`、真机判据）；
8. **HOCON 终局**：**已并入** [config-wire-redesign-plan.md](config-wire-redesign-plan.md) §5（两阶段退场）；本计划原写「降级为导入输入」；「全 lua 配置文件」是否意味着**最终删除** HOCON 资产链（`app/src/main/assets/profile/*.conf`）与 `profile-core` 的 HOCON 解析。
