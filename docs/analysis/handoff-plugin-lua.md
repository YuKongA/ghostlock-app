# 插件控制面（Lua）细则

> 分析类（≤8 KB）。配套 [handoff-payload-plan.md](handoff-payload-plan.md) §10 D31/D32、[handoff-payload-b1b2-runtime.md](handoff-payload-b1b2-runtime.md)、[handoff-payload-ops.md](handoff-payload-ops.md)。**自包含，不引用任何外部计划**。

## 1. 为什么插件适合 Lua（四条结构性理由）

1. **无 FFI ⇒ 能力面由宿主定义**：插件拿不到符号解析/重定位能力，能做什么完全取决于宿主注册了哪些 API（dofile/require/load/package 一律不注册）。
2. **运行期不做 mmap ⇒ 可放宽 PI 窗口约束**：现有规则是「PI waiter 存活期不得映射插件 `.so`」（`plugin/host.hpp:102`、`plugin/host.cpp:129`，`open()` 由 `host.hpp:21` 说明）。Lua 插件**不映射代码段**，故可在窗口内驻留；**对 `.so` 通道该约束原样保留**。
3. **失败天然软着陆**：`pcall`/`xpcall` + 指令预算 + 内存上限把插件错误限制为「该插件失败」，不产生 SIGSEGV/未定义行为；与 native 的 dirty 终止语义分层（脚本层失败 ≠ 进程 dirty）。
4. **一种运行时一套分发与哈希**：插件描述符 = 脚本返回的 **manifest 表**，与既有「描述符驱动」模型同构（`plugin/schema.hpp`、`plugin/wire.hpp`、`plugin/loader.hpp:82` 的 `sha256_file`），无需为 Lua 另立扫描/校验路径。

## 2. Lua 的反射能力事实清单（现实约束的来源）

| 有 | 说明 / 影响 |
|---|---|
| `debug.getinfo/getlocal/setlocal/getupvalue/setupvalue` | 可读改自身与调用栈局部/上值 ⇒ **不注册 `debug`** |
| `debug.getregistry` | 触达 VM 全局注册表 ⇒ 可绕过 `_ENV` ⇒ **不注册 `debug`** |
| `debug.getmetatable` / 元表与元方法（`__index`/`__newindex`/`__gc`/`__close`） | `__gc`/`__close` 可在回收点执行代码；宿主只对本方对象设元表 |
| `_ENV` 环境隔离 | **每插件一个 `_ENV`**：名字解析被限定；这是**作用域隔离，不是安全边界** |
| C API `lua_next` / `luaL_ref` / `luaL_newmetatable` | 宿主可枚举表、保活引用、建私有元表（注册期加载一次 + 保活） |
| `string.dump` / `lua_dump` | 可产出字节码；**本设计不消费外部字节码**（只跑源码解析结果） |
| `lua_getinfo` 的 `L` 模式 | 给**静态有效行**（可做诊断/预算归因，不用于安全判定） |
| `pcall`/`xpcall`、`coroutine` | 错误捕获与协作式并发；**协程不是抢占式线程**，预算仍由指令钩子统一计 |

| 没有 | 后果 |
|---|---|
| 静态类型 / 模块签名 | manifest 表必须**运行前静态扫描 + 加载期校验**（能力位、stage、params） |
| stock Lua 的 FFI | 不能直接触达外部函数 ⇒ 能力面 = 宿主 API 面 |
| 抢占式线程 | 单个死循环只能靠 `lua_sethook(LUA_MASKCOUNT)` 打断 |
| 同 VM 内强隔离 | 同一 VM 内两插件并非硬隔离 ⇒ 靠**不注册危险库 + 每插件 `_ENV` + 独立预算**，不宣称沙箱等价于进程隔离 |

## 3. 硬约束（写死，不可由 profile 放宽）

1. **不注册**：`debug` / `load` / `dofile` / `require` / `io` / `os` / `package`（反射即逃逸；动态代码即绕过静态扫描）。
2. **每插件 `_ENV` 沙箱 + 独立预算账户**：指令与内存**分开计**；单插件超限**只影响该插件**（具名失败），不波及其他插件。
3. **manifest 表即描述符，加载期校验**：能力位 ⊆ 宿主支持集；`stage` 合法（`contract/countermeasure.hpp:33`/`:39`：`PRE_ROUTE`/`POST_TERMINAL` 等）；`params` schema 与类型匹配；版本与哈希匹配（复用 `plugin/schema.hpp` + `loader.hpp:82`）。
4. **宿主 API 面 = 全体插件可达**；任何扩展（新函数/新参数）**必须列入受限副作用清单并单独评审**（复用 **D29**）。
5. **加载一次、常驻保活**：VM 常驻；插件在**注册期**加载一次并用 `luaL_ref` 保活；生命周期映射既有 host 的 **Registered → Opened → Closed**（`plugin/host.hpp:105` 的 `WindowState`、`:270` `open()`、`:279` `close()`、`:287` `registered()`）。
6. **窗口规则不因 Lua 放宽**：插件仍只在既有窗口内被调用（`plugin/registry.hpp:46` 的 `POST_TERMINAL`；`plugin/loader.hpp:62` 的链点集合）；Lua 只在**操作之间/窗口内**运行。

## 4. 分工（两种插件形态）

| 形态 | 适用 | 通道与约束 |
|---|---|---|
| **Lua 控制面/编排** | 需要按 stage 做决策、条件编排、参数组装、诊断汇总；不碰内核内存 | 共用 payload 的 Lua 5.4 VM、沙箱与预算；不映射代码 ⇒ 可在窗口内驻留 |
| **`.so` / LKM 内核型** | 内核热点、需直接内核交互的对策模块（含 relay/LKM 交互） | 保留现有通道：`.so` 探针 `plugin/probe.*`、描述符 `plugin/schema.hpp`、`plugin/kernel_channel.{hpp,cpp}`、host 生命周期；PI 窗口约束**不变**（`host.hpp:102`/`host.cpp:129`） |

- **统一注册**：两种形态进**同一 registry**（`plugin/registry.hpp`），遵守**同一 stage/能力/窗口规则**；差别只在「是否适用 Lua 沙箱与预算」。
- **冻结状态不变**：插件仍处于冻结；**解冻时按 D31 形态实现**（不恢复旧 `.so` 单一通道），`.so` 通道作为**内核型插件的保留通道**（D32）。

## 5. 判据（黑盒/白盒 + 极端值；见 [verification](handoff-payload-verification.md)）

| 判据 | 极端输入 → 预期 |
|---|---|
| 沙箱无文件/进程能力 | 插件调 `io.open` / `os.execute` ⇒ 报错终止（未注册） |
| 无反射逃逸 | 插件调 `debug.getregistry` / `debug.setupvalue` ⇒ 报错终止 |
| 预算隔离 | 单插件死循环/超内存 ⇒ **仅该插件**具名失败，其他插件继续 |
| 描述符校验 | manifest 声明未知能力位 / 非法 stage / params 类型不符 ⇒ **加载期**拒绝 |
| 哈希身份 | 插件文件被改一个字节 ⇒ sha256 不匹配，拒绝加载（`loader.hpp:82`） |
| 副作用边界 | 宿主 API 新增能力未列入受限副作用清单 ⇒ **评审门禁失败**（D29） |
| 环境隔离 | 插件 A 直接读插件 B 的 `_ENV`/上值 ⇒ 不可达（只能经宿主 API） |

## 6. 未决与边界

- 插件预算的**具体数值**（指令条数/内存字节）不在本文件钉死：需与 payload 脚本共用默认值后再定（与 D27/D29 同批判定）。
- 本文件**不申请**放宽任何既有约束：`KernelMemory`/`ChildTask`/`AddressDiscovery`/`CapabilityInterface` 等虚接口仍**不得进 PI 路径**（b0 §5）。
