# 契约层设计（virtual-first：backend 实现接口，steps 拼链）

> 状态：**设计草案，待批准**。2026-10-05 起**虚函数解禁**（`AGENTS.md` 已删禁令），本文以**抽象接口**为主形态重做。
> 配套：`config-wire-redesign-plan.md`（R7 能力并集）、`kernel-memory-batch1-plan.md`（KM 接口冻结）、
> `contract-first-chain-plan.md`（链组装）。

## 1. 命名原则

| 规矩 | 例子 |
|---|---|
| **能力 = 名词**，且该名词就是**接口名** | `KernelMemory`、`FileCacheWrite`、`AddressDiscovery` |
| **实现类 = backend 命名空间下的同名类** | `backend::cve_2026_43499::KernelMemory final : public contract::KernelMemory` |
| **枚举 = `*Kind` / `*Id`** | `CapabilityKind`、`ChainId`、`MemoryChannel`、`CarrierKind` |
| **错误 = `*Error`；结果 = `*Result<T>`** | `CapabilityError`、`CapabilityResult<T>` |
| **聚合 = 复数** | `Capabilities` |
| **契约里不得出现动词或 backend 名** | ✗ `run43499Exploit` |

## 2. 文件布局

```
contract/
  capability.hpp        # CapabilityKind（全 backend 并集）/ CapabilityError / CapabilityResult / CapabilitySet / CapabilityInterface
  kernel_memory.hpp     # KernelMemory（接口）+ MemoryChannel + CarrierKind + KernelMemoryOps（C ABI 束，见 §7）
  file_cache.hpp        # FileCacheWrite（接口）
  address_discovery.hpp # AddressDiscovery（接口，已存在，补错误语义）
  kernel_alias.hpp      # KernelAlias（接口）
  child_task.hpp        # ChildTask（接口）
  capabilities.hpp      # umbrella：Capabilities 聚合 + CapabilitySet 声明
  step.hpp              # Step（接口）+ StepContext + StepResult + StepError
  chain.hpp             # Chain（接口）+ ChainId + ChainContext
```

## 3. 能力词汇（`capability.hpp`）

```cpp
namespace ghostlock::contract {

/* 全 backend 并集（R7）。低 7 位与 CM 插件 ABI 的 glk_capability 逐位一致（static_assert 强制）。 */
/* 全 backend 并集（R7）。低 7 位与 CM 插件 ABI 的 glk_capability 逐位一致（static_assert 强制）。 */
enum class CapabilityKind : std::uint32_t {
    KernelRead = 1u << 0, KernelWrite = 1u << 1, KernelAlias = 1u << 2,
    ChildTask  = 1u << 3, FileCacheWrite = 1u << 4, Exec = 1u << 5, KernelHook = 1u << 6,
    /* 本树扩展（按 §3.5 的访问域划分） */
    AddressDiscovery = 1u << 7,          // 泄漏（43499 kernelsnitch）
    KernelDirectMap  = 1u << 8,          // direct-map 别名访问（含 image→direct map 翻译）
};
static_assert(std::to_underlying(CapabilityKind::KernelRead) == GLK_CM_CAP_KERNEL_READ);
/* …低 7 位同理… */

/* 未支持 = 一等错误（R7）：不静默、不返回 0、不偷偷换机制。 */
enum class CapabilityError : std::uint8_t {
    None = 0,
    Unsupported,   // backend 未声明提供该能力
    Unavailable,   // 声明提供，但当前不可用（通道未建立 / 设备不支持所选机制）
    Faulted,       // 提供了但执行失败
    Rejected,      // 参数/前置不合法，fail-closed
    Closed,        // 能力已显式关闭（终态）——「关闭后再用」是可检测缺陷，不与 Unavailable 混用
};
template <class T> using CapabilityResult = std::expected<T, CapabilityError>;
using CapabilityStatus = CapabilityResult<void>;

/* 编译期集合：backend 声明「提供什么」，step 声明「需要什么」。 */
class CapabilitySet final {
public:
    constexpr CapabilitySet &add(CapabilityKind) noexcept;
    [[nodiscard]] constexpr bool has(CapabilityKind) const noexcept;
    [[nodiscard]] constexpr bool satisfies(CapabilitySet required) const noexcept;
    [[nodiscard]] constexpr std::uint32_t bits() const noexcept;
};

/* 可选标记基类：需要多态持有（注册表/unique_ptr 容器）时用；消费者仍走**具体接口**。 */
class CapabilityInterface {
public:
    virtual ~CapabilityInterface() = default;
    [[nodiscard]] virtual CapabilityKind kind() const noexcept = 0;
};

} // namespace ghostlock::contract
```

## 3.5 访问域 AccessDomain —— 所有 backend 的读写目标（**先调研后设计**）

调研来源：`placeholder-backend-survey.md`（MITRE CAWG + 修复提交说明）+ `cve-2026-43284-backend-assessment.md`。

| backend | 读写目标（**域**） | 机制/原语 | 是否需要提权后步骤 |
|---|---|---|---|
| **43499** | **KernelVirtual**（单字 `*dest = value`，带结构副作用） | PI-futex stale waiter `rb_erase` 左子重连 | 需要 W1/W2/W3 |
| **64560** | **KernelVirtual**（UAF：`k_itimer`/posix_cpu_timer） | timer syscall + `exec` 竞态 | 同 UAF 类，或可复用提权框架 |
| **31431** | **KernelVirtual**（in-place AEAD 错位/重叠写） | `AF_ALG` AEAD socket | 未验证 |
| **43503** | **FilePageCache**（root 只读文件页缓存） | `SKBFL_SHARED_FRAG` + ESP `authencesn-ESN` 杂散写 | **可能不需要** |
| **43284** | **FilePageCache**（16B/次，任意文件偏移） | IpSec ESP-in-UDP CBC-IV | 不需要（走 UMH 终态） |
| **23274** | **未知**（描述像 DoS/panic，CVSS 与描述矛盾） | `xt_IDLETIMER` rev0 未初始化 timer | 未验证，**暂不作为 backend** |

**域枚举（只用于声明与诊断）**

```cpp
enum class AccessDomain : std::uint8_t {
    None = 0,
    KernelVirtual,    // KASLR 后的内核虚拟地址（43499/64560/31431 的写目标）
    KernelDirectMap,  // direct-map 别名（内核镜像访问、vivo vr_guard 用）
    KernelImage,      // 内核镜像内相对地址（slide 前；经 KernelAlias 翻译）
    FilePageCache,    // 文件偏移 → 页缓存（43284/43503）
    UserStaging,      // 用户态暂存/喷溅缓冲（**利用自身内存，不入契约**）
};
```

**设计决定：接口按「地址种类」分型，不用一个 enum 混装地址**（避免把文件偏移当 VA 传）：

| 接口 | 地址种类 | 覆盖的域 | 谁提供 |
|---|---|---|---|
| `KernelMemory` | 内核 VA（含 direct-map VA） | KernelVirtual / KernelDirectMap | 43499（未来 64560/31431） |
| `KernelAlias` | image 相对 → direct-map VA 的**翻译** | KernelImage → KernelDirectMap | 43499（vivo 对策用） |
| `FileCacheWrite` | 文件偏移 | FilePageCache | **43284 与 43503 共有 → 真正可共享的能力** |
| `AddressDiscovery` | ——（泄漏） | —— | 43499（kernelsnitch） |
| `ChildTask` | ——（当前 child task） | —— | 任一需要标记子任务的 backend |

**两个由调研直接得出的结论**
1. **43503 与 43284 同域**（都是页缓存写）→ `FileCacheWrite` 是**第一个真正跨 backend 共享的能力**（计划里 B4「跨 backend 共享」的实证依据）；
2. **43499/64560/31431 同为内核 VA 域，但 primitive/route 完全不可共享**（调研结论 1）→ 共享的是**能力接口**，不是机制；这与「契约优先、实现私有」一致。

## 3.6 能力状态机（阶段控制）

**动机**：能力不是「有/无」的静态事实，而是**有生命周期**的。典型例子：**43499 的任意读必须先建立 Tier 2 通道**
（Tier 1 引导写 → 通道建立 → 才能 read/write）；43284 的页缓存写要先建立 IpSec SA + helper + 载体。

```cpp
enum class CapabilityState : std::uint8_t {
    NotSupported = 0,   // 该 backend **从不**提供（静态事实，来自 declared()）
    NotAvailable = 1,   // 提供，但**尚未就绪**（需要先前步骤初始化；或本设备不支持所选机制）
    Available    = 2,   // 当前可用
    Closed       = 3,   // **已被显式关闭（终态）**：通道/资源已拆，不可再跃迁回 Available
};

/* 生命周期（不是简单的数值序，用显式表比较）：
 *   NotSupported ──(永不)─────────────────────────────► 终态
 *   NotAvailable ──establish()──► Available ──close()/release()──► Closed（终态）
 *                    ▲                                    │
 *                    └──── 不得从此跃迁 ──────────────────┘
 * 语义：NotAvailable = 「还没开」；Closed = 「开过了、已关」——两者都不可用，但**原因不同**，
 * 诊断必须区分（混为一谈会掩盖「关闭后再用」这类缺陷）。 */
[[nodiscard]] constexpr bool satisfies(CapabilityState have, CapabilityState need) noexcept {
    if (have == CapabilityState::Available) return need == CapabilityState::Available;
    if (have == CapabilityState::NotAvailable) return need == CapabilityState::NotAvailable;
    return false;   // NotSupported / Closed：任何 need 都不满足
}

/* 状态是唯一真相：错误由状态**映射**（不是数值相等——两者枚举域不同，0≠1）。 */
[[nodiscard]] constexpr CapabilityError error_for(CapabilityState s) noexcept {
    switch (s) {
        case CapabilityState::NotSupported: return CapabilityError::Unsupported;
        case CapabilityState::NotAvailable: return CapabilityError::Unavailable;
        case CapabilityState::Closed:       return CapabilityError::Closed;
        case CapabilityState::Available:    return CapabilityError::None;
    }
    return CapabilityError::Faulted;
}
/* 测试断言这张映射表的每个输入/输出（真值表），不做数值比较。 */
```

**三重语义**

| 状态 | 含义 | 调用结果 |
|---|---|---|
| `NotSupported` | backend 编译期就没这个能力（`declared()` 里没有） | `CapabilityError::Unsupported` |
| `NotAvailable` | 有能力，但**顺序不对/尚未建立**（或设备/机制不支持） | `CapabilityError::Unavailable` |
| `Available` | 可用 | 正常执行 |
| `Closed` | **已显式关闭**（release/teardown 之后），**终态**：不可再跃迁、不可再用 | `CapabilityError::Closed` |

**状态是唯一真相**：`CapabilityError` 不另立一套判断——调用失败原因**由状态派生**，避免「两个来源说法不一」。

**接口形态（按能力 + 按机制查询）**

```cpp
class KernelMemory : public CapabilityInterface {
public:
    [[nodiscard]] virtual CapabilityState state(MemoryChannel) const noexcept = 0;   // 每机制一个状态
    [[nodiscard]] virtual CapabilityState state(CarrierKind)  const noexcept = 0;
    /* 跃迁（arming）：建立通道/载体。成功 → 对应 state 变 Available。 */
    virtual CapabilityStatus establish(MemoryChannel, CarrierKind) noexcept = 0;
    virtual CapabilityStatus release(CarrierKind) noexcept = 0;                       // 回滚用
    …
};
```

**阶段控制靠「声明 + 校验」，不靠约定**

step 除 `requirements()` 外再声明两件事：

```cpp
struct CapabilityRequirement { CapabilityKind kind; CapabilityState minimum; };   // 我要它至少到 Available

class Step {
    [[nodiscard]] virtual std::span<const CapabilityRequirement> requires() const noexcept = 0;
    [[nodiscard]] virtual std::span<const CapabilityKind>        arms()     const noexcept = 0;  // 我把谁变成 Available
    …
};
```

**链级校验（这就是「阶段控制」的落点）**：按 `steps` 顺序折叠状态集合（初始 = backend 的 `declared()` 减去
`NotSupported`），对每个 step 断言 `requires ⊆ 当前 Available`，然后把 `arms` 并入集合。

- **编译期**：`constexpr` 折叠 → `static_assert`（顺序不可行 = 编译失败，例如「读在 establish 之前」）；
- **注册期**：host 测试对每个已登记 chain 重跑同一折叠（防止模板外的手工组装绕过）；
- **运行期**：设备/机制差异导致的 `NotAvailable` → 该步 fail-closed，**不跳过、不降级**；
- **诊断**：每步执行前后记录关键能力状态（`cap=<kind> state=<…>`），使「为何跳过/失败」可回答
  （`NotSupported` 与 `NotAvailable` 语义不同，不能混为一谈）。

**回滚**：arming 的 step 必须在 `rollback()` 里 `release()`；链层保证终结点恰一次（§6）。

## 3.7 两个真实 backend 的能力表（**代码取证**，2026-10-05）

取证结论：`KernelMemoryOps` **目前没有任何实现**（仅契约声明）；`FileCacheWriteOps` 由 43284 实现
（`pagecache.cpp:297-300` + `real_ops.cpp:174/251/520`）；`AddressDiscoveryOps` 由 43499 的
kernelsnitch 适配器实现（`backend/cve_2026_43499/leak/address_discovery.h`，`support/util.cpp:647` 消费）；
**43499 没有通用内核读原语**（全仓 grep 无 `read64`/`copy_to_user`）。

### 43499

| 能力 | 现状（作为**契约能力**） | 状态语义 | 目标（C/KernelMemory 落地后） |
|---|---|---|---|
| `KernelMemory`（read） | **NotSupported**：无通用读；只有泄漏类侧信道 | —— | `Available`（**必须先 `establish` Tier 2 通道**，否则 `NotAvailable`） |
| `KernelMemory`（write） | **NotSupported**：内部有 `attack_write<M>`，但**固定值、单字、未暴露** | —— | `Available`（Tier 2 任意值/掩码写）；Tier 1 `write_zero` 作引导 |
| `AddressDiscovery` | **Available**：kernelsnitch，已活体接线（A3-2 ③） | 始终可调用（内部 fail-closed 写全零，消费方映射哨兵） | 不变 |
| `KernelAlias` | **NotSupported**：翻译存在于 address state（`addresses.data_alias`），**未暴露为能力** | —— | `Available`（vivo `vr_guard` 与镜像访问需要） |
| `ChildTask` | **NotSupported**：`child_task` 在 victim/W2/W3 内部使用，**未暴露** | —— | `Available`（每步刷新） |
| `FileCacheWrite` | **NotSupported** | —— | NotSupported（域不同） |

### 43284

| 能力 | 现状（作为**契约能力**） | 状态语义 | 目标 |
|---|---|---|---|
| `FileCacheWrite` | **提供**（`pagecache` 实现 `write16`；`real_ops` 在 page/hook 两路绑定） | **典型三态**：`page_cache_ready()` 为假 → `NotAvailable`；`apply_hook`+载体就绪后 `establish` → `Available`；`release_hook`/链结束 → `Closed` | 不变 |
| `FileCacheRead`（建议纳入 `FileCacheWrite` 的读改写协议） | 协议内部有旧页读取（crash_dump 桥 `pread`/`read16`，用于 verify 与回滚） | 与 write 同步的三态 | 见待确认 8 |
| `KernelMemory` | **NotSupported**（域不同：改文件而非改内核状态） | —— | NotSupported |
| `AddressDiscovery` / `KernelAlias` / `ChildTask` | **NotSupported** | —— | NotSupported |

### 由表得出的两点

1. **目前只有两个能力真正落地**（43499 的 `AddressDiscovery`、43284 的 `FileCacheWrite`），
   其余是「契约先行、实现待批」——这解释了为什么 `capabilities.hpp` 里 `KernelMemoryOps` 至今没有 provider；
2. **43284 的 `FileCacheWrite` 是状态机的最佳样板**：`NotAvailable`（页缓存未就绪）→ `establish` →`Available` → `close` → `Closed`，
   且 43499 的 `KernelMemory` 将来也走同一条路（先 establish 通道），**状态机不是为 43499 特设**。

## 3.8 countermeasure 插件能否调用这些能力？**能，但要补四条规则**

**取证**：`contract/glk_cm_abi.h` 已经把能力族的 **(B) C 形态**暴露给插件——
`glk_host_ops{ read_u64, write_u64, read_bytes, write_bytes, zero_word, image_to_direct_map,
query_u64, query_str, log, child_task }`（`:123-143`）；模块用 `required_caps`（OR of `glk_cm_cap` 位）
声明需求，宿主在加载期与自己的授予集比对，**reserved 位直接拒绝**（`:82-96,175`）。

**映射（CM ABI ↔ 本契约的能力族）**

| CM ABI | 契约能力族 | 状态 |
|---|---|---|
| `read_u64/write_u64/read_bytes/write_bytes/zero_word` | `KernelMemory`（`zero_word` = Tier 1 引导） | 宿主已实现 `KERNEL_READ/KERNEL_WRITE` |
| `image_to_direct_map` | `KernelAlias` | 宿主已实现 `ALIAS` |
| `child_task`（值字段） | `ChildTask` | 宿主已实现 `CHILD_TASK` |
| ——（保留位） | `FileCacheWrite` / `Exec` / `KernelHook` | 保留 |
| `query_u64/query_str/log` | 非能力（诊断/查询） | —— |

**四条规则（让两套系统一致）**

1. **词汇同源**：`glk_cm_cap` 位与 `contract::CapabilityKind` 必须**逐位一致**（`static_assert` 强制）——
   否则「插件声明的能力」与「契约定义的能力」会各自漂移。
2. **声明 ⊆ 授予**：插件只能调用 `required_caps` 里声明、且宿主授予的能力。
   这与 §6「step 需求 ⊆ 所属 backend 声明集」是**同一条规则**，只是发生在 C ABI 边界。
3. **状态机同样适用**：插件调用必须尊重 `CapabilityState`。例：`ON_STAGE @ PRE_SPAWN` 时 Tier 2 通道
   可能还没 `establish` → `KernelMemory` 为 `NotAvailable` → 调用必须返回 `Unavailable`，
   **不得静默 no-op**。→ **CM ABI 需增补**：`glk_host_ops.state(cap)` 查询（或让每个 op 返回状态派生错误码），
   并把裸值 `child_task`（现在「POST_SPAWN 起有效，否则 0」——**0 与「未就绪」不可区分**）改为
   状态化访问（`state(CHILD_TASK)` + getter 返回错误）。
4. **时序边界**：插件调用本身是**跨 `.so` 的间接调用**；若注册在**竞争窗口内**的阶段，等于往 PI 窗口再加一层间接跳转，
   与 §7 的张力叠加。→ **插件只允许注册在窗口外阶段**；窗口内阶段不接受外部插件（或必须显式声明 + 真机门禁）。

**一条建议（防越权改控制流）**：插件**可以用**能力，但**不得 arming**（`establish/close`）——
arming 属于攻击关键路径，由树内 step 负责；否则插件可以改变攻击链的阶段推进。

## 3.9 推演：vr.ko 对策的「能力 × 阶段」矩阵

**代码取证**（vivo 两对策现为 `ancillary::AncillaryPolicy`，经 `AncillaryController` 在两处调用）：

| 阶段 | 时机（代码位置） | 对策 | 调用的能力 | 所需状态 | 谁 arming | 失败语义 |
|---|---|---|---|---|---|---|
| **PreSpawn** | W1b 之后、victim spawn **之前**（`steps.cpp:409-420`；此后 SELinux 已 permissive） | `vr_guard`（清 `sys_exit tp->funcs`） | ① `KernelAlias::to_direct_map(image)` ② `KernelMemory::write_zero(va)`（≤5 次重试，`vr_guard.cpp:60/86/93`） | `KernelAlias@Available`、`KernelMemory@Available`（**Tier 1 的 `write_zero`，无需 establish**） | `KernelAlias` 由 43499 会话地址状态提供 | **fail-soft**：warning + 跳过（不阻断攻击） |
| **PostSpawn** | 子进程就绪后、**W2 verify 之前**（`steps.cpp:171-200`） | `vr_task_tag`（清 per-task tags） | ① `ChildTask::current()` ② `KernelMemory::write_zero(flags+tagA)` ③ `KernelMemory::write_zero(tagB)`（`vr_task_tag.cpp:81/89/91`） | `ChildTask@Available`（spawn 后才有值）、`KernelMemory@Available` | child task 由 victim 步骤提供 | **fail-soft**：同上 |
| **PreHandoff** | 枚举存在（`ancillary_policy.hpp:20-24`），vivo 两对策**不用** | —— | —— | —— | —— | —— |

**推演得出的五条结论**

1. **两个对策都在 PI 竞争窗口之外**（PreSpawn 在 spawn 前；PostSpawn 在两次写之间、竞态线程空闲）
   → 天然满足 §3.8 规则 4「插件只允许在窗口外阶段」。
2. **它们只要 Tier 1**（`write_zero` + `image_to_direct_map` + `child_task`），**不需要 Tier 2 任意读/写**。
   因此即使 Tier 2 通道尚未 `establish`（`NotAvailable`），vivo 对策仍能运行——状态机不会误伤它们。
3. **反例验证状态机的价值**：若将来某对策想在 **PreSpawn 读内核内存**，此时 Tier 2 必为 `NotAvailable`
   → 状态机**明确拒绝**（`Unavailable`），而不是给它一个假的成功；这正是「先启动才能读」要防的错。
4. **`child_task == 0` 的魔法值应升级为状态**：现在靠 `vr_task_tag.cpp:70` 的约定判断「未就绪」；
   改成 `ChildTask@NotAvailable` 后，插件拿到的是**显式错误码**而非一个看起来合法的 0。
5. **现有代码已在用 §7(b) 的模式**：`AncillaryOps` 是**中性策略**（不知道 backend/route），
   而 `write_zero = Cve43499Primitives::zero_word<M>` 是**编译期绑定到 route `M`** 的原语
   （`steps.cpp:181/412`）——即「编排中性、窗口内编译期绑定」不是新发明，而是既有事实，
   新契约只是把它从"注释约定"变成"类型保证"。

**对 CM ABI 的直接影响**：插件要做的正是上表这几类调用 → `glk_host_ops` 目前给的
`write_zero`/`image_to_direct_map`/`child_task` 已覆盖，但需按 §3.8 增补 `state(cap)`，
并且**对策不得 arming**（`write_zero` 是 Tier 1 现成能力，无需跃迁；Tier 2 的 `establish` 属攻击步职责）。

## 3.10 归一：**countermeasure ABI 就是 contract 的 C 导出**（不再并列设计）

**问题**：现在有两套并行产物——C++ 虚接口（本契约）与 `glk_cm_abi.h`（C 结构 + **另一份**能力位表 + `glk_host_ops`）。
两者描述同一件事（能力），却各自维护 → 必然漂移（`glk_cm_cap` 与 `CapabilityKind` 已需 `static_assert` 兜底）。

**目标形态：一份契约，两种投影**

```
contract/                      ← 唯一权威（能力词汇 + C++ 接口）
  capability.hpp                 CapabilityKind / CapabilityState / CapabilityError / CapabilitySet
  kernel_memory.hpp  …           能力接口（C++ 虚）
  abi/
    glk_contract_abi.h           **C 导出**（原 glk_cm_abi.h 改名）：
                                   glk_capability（位，与 CapabilityKind 同源）
                                   glk_capability_state（与 CapabilityState 同源）
                                   glk_contract_ops（原 glk_host_ops；由 C++ 接口**适配生成**）
                                   glk_module / glk_stage / glk_trigger（插件侧声明与回调）
    export.hpp                   C++ 侧**唯一**实现：把 C++ 接口适配成 glk_contract_ops
                                 （一处实现，避免两套并行）
plugin/                        ← 宿主侧插件设施（**改名**，见下）
  loader.*  registry.*  controller.*  stage.*
```

**关键规矩**

1. **能力与状态只在 `contract/` 定义一次**；C 头里的位/枚举**由它导出**（`static_assert` 或生成），
   不允许出现第二份手写表。
2. **`glk_contract_ops` 不是手工维护的接口**：它是 `export.hpp` 把 C++ 能力接口**适配**出来的函数指针束——
   新增能力先加 C++ 接口，再在 export 里加一个适配函数；两处都在同一提交内，且有测试锁死。
3. **插件只能看到 C 导出**（跨 `.so` 不能吃虚表），树内 step 用 C++ 接口——**同一语义、两种投影**。
4. **状态机必须进 C 导出**：`glk_capability_state` + `glk_contract_ops.state(cap)`；
   插件拿到 `NotSupported/NotAvailable/Available/Closed` 而不是「0 或失败」。
5. **阶段/触发器不属于能力**：`glk_stage`/`glk_trigger` 是**插件设施**的概念（何时被调用），
   与「能调用什么能力」正交——因此它们留在 `plugin/`，不进 `contract/`。

**改名（"再改个名字"）建议**

| 现在 | 目标 | 理由 |
|---|---|---|
| `contract/glk_cm_abi.h` | `contract/abi/glk_contract_abi.h` | 它是**契约的 C 导出**，不是「对策专用」ABI |
| `glk_cm_cap` / `glk_cm_capability` | `glk_capability` | 与 `CapabilityKind` 同名同源 |
| `glk_host_ops` | `glk_contract_ops` | 它是契约的宿主侧函数表 |
| `glk_cm_module` / `glk_cm_stage` / `glk_cm_trigger` | `glk_module` / `glk_stage` / `glk_trigger` | 去掉 `cm` 前缀（不再暗示「对策专用」） |
| `platform/countermeasure/` + `ancillary/` | **`plugin/`（已裁决 2026-10-05）** | 它是对策**设施**（loader/registry/controller/stage），不是能力层 |

**收益**：能力只有一处定义；C ABI 不可能与 C++ 契约漂移；插件与树内 step 看到同一套语义；
R8（一个子系统一个家）与本节合成一条：**`contract/`（含 `abi/`）+ `plugin/`**。

## 3.11 推演：43284 侧（UMH/LKM + 对策插件）与「要不要给 .ko 加接口」

**代码取证**：`tools/lkm/ghostlock/ghostlock.c` 内部**已有内核能力**——
① `kallsyms_lookup_name`（kprobe 技巧取任意符号，`:98-107`）；② 置 SELinux permissive；
③ `call_usermodehelper` 跑一条命令；④ 可选 **Samsung Defex kprobe 对策**（`:70/129-131`）。
**但对外只有加载期 module_param**（`cmd`/`permissive`/`defex`/`restore_enforce`），**没有运行时请求通道**。
43284 终端的就绪探测是**只读**的（`/dev/dfm0` + `/proc/modules` 含 `kernelsu`，`umh_forward.cpp:21-35`）。

| 阶段 | 谁在跑 | 可用的契约能力 | 状态 | 对策插件能做什么 |
|---|---|---|---|---|
| PRE_WRITE（载体/patch#1 之前） | native | `FileCacheWrite` | `NotAvailable`（`page_cache_ready()` 假） | 无（尚未 arming） |
| WRITE → HOOK → TRIGGER | native（页缓存写 → init 命中 libc++ hook） | `FileCacheWrite` | `Available`（由链 arming） | 只能做**页缓存层**对策；内核内存 = `NotSupported` |
| insmod 之后（**内核态**） | **我们的 .ko** | 内核符号/写/kprobe **存在于 LKM 内部，但未暴露** | —— | **Defex/策略/task 标记类对策只能在这里做**（现在是硬编码 `defex=1`） |
| PRE_TERMINAL / POST_TERMINAL | native（只读探测） | 无写能力（`FileCacheWrite` 已 `Closed`） | `Closed` | 仅观测 |

### 结论：**要**，而且这是 43284 路径支持内核态对策的唯一途径

理由：① 43284 路径**没有 `KernelMemory`**（§3.7，`NotSupported`）——native 侧拿不到内核任意读写；
② 大多数厂商对策（Defex、SELinux 策略类、task 标记、vr_guard 类）**需要内核内存写或内核 hook**；
③ 这些机制**LKM 已经在用**，只是**只以加载期参数暴露**。

**建议的接口形态**

1. LKM 暴露**版本化请求通道**（`/dev/glkctrl` 或固定 procfs 文件；带 `abi_version` + **命令白名单**）；
2. native 侧实现 `contract::KernelMemory` / `KernelHook` 的**代理 provider**，把调用转发进 LKM；
3. **优先把它建模成「又一种机制」**而不是新接口族：`MemoryChannel` 增 `LkmProxy`，
   `supports(LkmProxy)` 即「LKM 已加载且通道就绪」——与维护者「全部机制 + enum 显式选」的裁决一致；
4. `KernelHook`（CM 位表已预留 `GLK_CM_CAP_KERNEL_HOOK`）在 43284 路径由 LKM 提供，`establish` = 注册 hook；
5. **状态映射**：insmod 前 `NotAvailable` → 通道建立后 `Available` → 卸载/自卸载后 `Closed`（终态，不可再用）；
6. 插件授予集纳入 LKM 能力（§3.8 规则 2：`required_caps ⊆ 授予集`）。

### ⚠️ 安全权衡（必须写进实现批次）

- LKM 驻留 + 通用读写接口 = **新的持久攻击面**；当前 LKM 是「**4 步固定动作 + 自卸载**」，可审计性高；
- 因此接口应是**受限命令白名单**（如 `hook_symbol` / `write_symbol_field` / `mark_task`），
  **而不是** `read(addr,len)`/`write(addr,len)` 的通用内核后门；
- 若确实需要通用读写（`KernelMemory` 由 LKM 提供），须作为**显式高风险能力**：在 wire/UI 单独授权，
  并把每次调用记入诊断（`cap=KernelMemory mechanism=LkmProxy addr=…`）；
- 自卸载语义要保留：**对策插件用完即走**优于「留一个常驻后门」。

## 3.12 LkmProxy 与 KernelMemory 的关系：**机制，而非并列能力**（已裁决方向）

**问题**：`LkmProxy` 是否覆盖 `KernelMemory` 的操作？若是，能否把它写成 `KernelMemory` 的一个「overload」？

**答案：是。`LkmProxy` 不是新能力，而是 `KernelMemory` 的又一种机制（`MemoryChannel` 的一个取值）**，
因此它**覆盖全部 KernelMemory 原语**：`read` / `write` / `write_zero` / `update_bits` / `supports`。
「写成 overload」的正确落法不是语言级重载，而是**一个可复用的适配实现**：

```cpp
/* 唯一的 LKM 后端适配器：任何"能托管 LKM"的 backend 复用它，
 * 不必各写一份（这是「backend 实现 contract 接口 + steps 拼链」的直接收益）。 */
class LkmProxyKernelMemory final : public contract::KernelMemory {
public:
    explicit LkmProxyKernelMemory(LkmChannel &ch) noexcept : ch_(ch) {}

    [[nodiscard]] bool supports(MemoryChannel m) const noexcept override {
        return m == MemoryChannel::LkmProxy && ch_.established() &&
               ch_.abi_version() == kLkmChannelAbiVersion;      // 版本不匹配 = 不支持
    }
    [[nodiscard]] CapabilityState state(MemoryChannel m) const noexcept override {
        if (m != MemoryChannel::LkmProxy) return CapabilityState::NotSupported;
        if (!ch_.established())           return CapabilityState::NotAvailable;   // LKM 未加载/通道未建
        if (ch_.closed())                 return CapabilityState::Closed;         // 自卸载后 = 终态
        return CapabilityState::Available;
    }
    CapabilityStatus establish(MemoryChannel m, CarrierKind) noexcept override;  // 建立通道（需 LKM 已 insmod）
    CapabilityStatus release(CarrierKind) noexcept override;                      // 关闭通道

    CapabilityResult<std::uint64_t> read (std::uint64_t va, std::span<std::byte> out, MemoryChannel) noexcept override;
    CapabilityStatus write(std::uint64_t va, std::span<const std::byte> in, MemoryChannel) noexcept override;
    CapabilityStatus write_zero(std::uint64_t va) noexcept override;
    CapabilityStatus update_bits(std::uint64_t, std::uint64_t, std::uint64_t, MemoryChannel) noexcept override;
    [[nodiscard]] CapabilityKind kind() const noexcept override { return CapabilityKind::KernelWrite; }
private:
    LkmChannel &ch_;   // plugin::LkmChannel：版本化请求通道（命令白名单，见 §3.11 的安全权衡）
};
```

**哪些 backend 能提供它**（`SupportsLkmProxy` 的条件 = 「该 backend 的终态能加载我们的 LKM」）

| backend | 能否 | 说明 | 状态时序 |
|---|---|---|---|
| **43284** | ✅ 天然 | 链本身就是「写页缓存 → 触发 → insmod 我们的 .ko」 | insmod 前 `NotAvailable` → 通道建立 `Available` → 自卸载 `Closed` |
| **43499** | ✅ 可（handoff 后） | root child 能 insmod；但发生在 W3/handoff **之后** | W3 前 `NotAvailable` → 加载后 `Available` |
| 64560 / 31431（预留） | 视终态 | 同为内核内存域，若复用 root-child/UMH 终态则可 | 同上 |
| 43503 | ❌ | 页缓存域，不需要内核内存 | `NotSupported` |

**要点**
1. **一个适配器，多 backend 复用** → LKM 路径从「每个 backend 各写一套」变成**契约的共享实现**，
   这正是「契约优先」相对旧架构的核心收益；且 `MemoryChannel::LkmProxy` 与
   「全部机制 + enum 显式选」的裁决**完全一致**（机制是可数的取值，不是一个新接口）。
2. **状态机把它管住**：`NotAvailable`（LKM 未加载）↮ `Available`（通道就绪）→ `Closed`（自卸载后终态）；
   43499 上「W3 前调用 LkmProxy」会被**明确拒绝**（`Unavailable`），而不是给个假成功。
3. **安全模型（已裁决 2026-10-05）**：**不设授权门**——威胁模型是「操作者可信」（用户本就在执行内核提权）。
   因此 `LkmProxy` 提供**完整** `KernelMemory` 原语，**无需 wire/UI 授权字段**；
   残余风险（执行后驻留）改由**尽快自卸载**这一硬约束来缓解（见下）。
   仍保留**诊断记录**（`cap=KernelMemory mechanism=LkmProxy` + 调用结果），因为可审计性对排障与门禁都有用——
   这是"不授权"而非"不记录"。
4. **版本化**：`kLkmChannelAbiVersion` 不匹配即 `NotSupported`（fail-closed），
   避免「native 与 .ko 各自演进而静默错配」。

### 3.12.1 **尽快自卸载** 是一等生命周期约束（替代授权门）

既然不设授权，LKM 的**驻留窗口**就是主要风险面，必须由契约与链共同保证它尽可能短：

1. **驻留窗口 = 一个显式区间**：`insmod → establish(LkmProxy) → [需要内核能力的对策略/步骤] → unload → Closed`；
   链层负责把这个区间**包住**，窗口之外不允许再有 `LkmProxy` 调用（状态机保证：`Closed` 后调用 = 错误）。
2. **谁触发卸载**：由链在「最后一个使用 LkmProxy 的步骤」之后立即触发（不是等整条攻击结束）；
   若某步骤失败，**回滚路径也必须卸载**（fail-closed：宁可丢弃能力，也不留驻留模块）。
3. **卸载即 `Closed`**：`state(LkmProxy)` 转 `Closed`（终态）；**不得**重新 `establish`（同一 run 内不可复用；
   若确需再次使用，必须是新的 insmod，即一次新的生命周期）。
4. **对策插件的调度受限**：需要 `LkmProxy` 的插件**只能注册在驻留窗口内**的阶段；
   窗口外阶段注册的插件若声明了 `KernelMemory` 需求 → **注册期校验失败**（§6 绑定规则）。
5. **可验证**：诊断记录 `lkm_window{insmod_t, unload_t, calls=n}`，门禁可据此断言「窗口短且无窗口外调用」。

### 3.12.2 δ 批设计：LKM 版本化请求通道（`LkmProxy` 的实现）

**目标**：让插件/步骤在驻留窗口内获得**完整 `KernelMemory` ops**（读/写/清零）+ `KernelAlias`，
而**不引入授权门**（维护者裁决）；安全性由**窗口最短 + 卸载即 Closed**保证。

**通道形态**（内核侧，替换现有 module_param-only 接口）

```c
/* contract/abi/glk_contract_abi.h —— 与 C++ 契约同源导出 */
#define GLK_LKM_ABI_VERSION 1u
enum glk_lkm_op { GLK_LKM_PING=0, GLK_LKM_READ, GLK_LKM_WRITE, GLK_LKM_WRITE_ZERO,
                  GLK_LKM_DIRECT_MAP, GLK_LKM_QUERY, GLK_LKM_LOG, GLK_LKM_UNLOAD };
struct glk_lkm_req {
    uint32_t abi_version;   /* 必须 == GLK_LKM_ABI_VERSION */
    uint32_t op;
    uint64_t addr;          /* READ/WRITE/WRITE_ZERO: 目标虚拟地址 */
    uint64_t value;         /* WRITE: 值；DIRECT_MAP: 输入 image 地址 */
    uint32_t len;           /* READ/WRITE 字节数（<= GLK_LKM_MAX_XFER） */
    uint32_t status;        /* 返回：0 = ok，否则 -errno */
};
```

- 设备节点：`misc_register` → `/dev/glk`（`glk_lkm_fops`：`open/ioctl/release`）；`GLK_LKM_UNLOAD` 触发自卸载。
- **地址校验**：`addr` 必须落在 direct-map 区间内（用既有的 `direct_map_end` 判定），否则 `-EFAULT`（防止变成任意内核指针写口）。
- **版本**：native 打开后先 `PING` 校验；不匹配 → `CapabilityError::Unsupported`（fail-closed，不猜）。

**native 侧适配器**（`plugin/kernel_channel.{hpp,cpp}`）

- 实现 `contract::KernelMemory`（`read/write/write_zero` → `READ/WRITE/WRITE_ZERO`）与 `contract::KernelAlias`（`DIRECT_MAP`）；
- `state()`：设备未打开 → `NotAvailable`；打开且 `PING` 通过 → `Available`；卸载后 → `Closed`；
- **只允许在窗口内实例化**：由链在 `insmod` 成功后构造，卸载时销毁。

**窗口执行（链侧强制）**

- 在「最后一个声明 `KernelMemory`/`KernelAlias` 需求的步骤」之后**立即**发 `UNLOAD`；
- 失败/回滚路径同样发 `UNLOAD`（fail-closed）；
- 卸载后设备节点消失，任何后续调用 = `CapabilityError::Closed`（测试断言）。

**插件执行位置**：内核态**不执行**插件本体；native 经通道转发（裁决 10）。插件仍住在 `plugin/`，
内核侧只是能力提供者——这让插件代码、参数 schema、诊断全部留在用户态，内核面最小化。

### 3.12.3 δ 批实现裁决（2026-10-05，实现后回填）

1. **`glk_lkm_req.value` = 用户态缓冲区地址**（不是 8 字节值）：`READ/WRITE` 用 `addr` = 内核 direct-map VA、`value` = 用户 buffer；
   `DIRECT_MAP` 用 `value` = image 地址、成功时把别名**回填 `addr`**。理由：结构体无独立数据缓冲字段，
   只有这样才能承载「完整 `KernelMemory` ops（读/写/清零）」。字段名/顺序/位宽不变。
2. **`resident` 默认 = 1**（常驻，等待 `UNLOAD`），兜底 `GLK_LKM_RESIDENT_TIMEOUT_MS = 5000`：
   shellcode 槽位表固定 7 项（`exe_path` 19B / `ko_target` 64B）**无法追加 argv**，故不能用 `insmod … resident=1`；
   改为默认常驻 + native 在窗口结束时 `UNLOAD`；native 未接线时 5s 兜底自卸载（行为不回归，仅多 ≤5s）。
3. **内核侧 ABI 常量是镜像**：`tools/lkm/ghostlock/ghostlock.c` 无法 include 用户态树（out-of-tree 容器构建），
   故 `glk_lkm_op`/`glk_lkm_req`/ioctl 号在两侧各有一份；`contract/abi/glk_contract_abi.h` 为权威，
   需一侧改动时两侧同步并由真机门禁兜底（后续可加 host 对拍脚本）。
4. **地址合法性 ≠ 授权**：`glk_lkm_addr_ok` 用 `virt_addr_valid()` 校验 `addr` 与 `addr+len-1` 均在 direct-map 内；
   越界/溢出/`len=0`/`len>MAX_XFER` 一律 `-EFAULT`。不设授权门（维护者裁决）。

### 3.12.4 修正：驻留生命周期 = **客户端会话**，不是定时器（维护者 2026-10-05 指出）

**问题**：δ 实现把「何时卸载」交给 `GLK_LKM_RESIDENT_TIMEOUT_MS=5000` 兜底超时。这有两个错：
1. **语义错**：定时器可能在**插件执行中途**把模块抽走（对策被截断，后果不可预期）；
2. **时序错**：窗口长度应由「要做多少事」决定，而不是某个拍出来的常数。

**正确模型**（三条）：

1. **插件调用一律同步**：窗口内 native **等待每次插件调用返回**再继续；**禁止**异步/后台插件在窗口外存活。
   窗口 = `open → 每个插件同步执行完 → close(UNLOAD)`，不存在「插件还在跑但窗口已关」的状态。
2. **驻留生命周期绑定打开者会话（fd）**：内核记录 opener 的 **PID**；`release()`（含进程退出/崩溃时内核强制关闭 fd）
   → 唤醒 `module_init` 返回 `-E2BIG` 自卸载。因此**正常路径根本不依赖任何超时**：
   native 显式 `UNLOAD` 是快速路径，**fd 关闭是兜底**，覆盖 native 崩溃/被杀的路径。
3. **定时器降级为「泄漏 watchdog」**：仅用于「fd 被泄漏且永不关闭」这一病态情形；
   取**远超正常窗口**的值（默认 `GLK_LKM_WATCHDOG_MS = 60000`，可用 module_param 覆盖），
   **不再**是正常卸载路径。正常窗口应为**亚秒级**（几十次 ioctl）。

**验证方式**（真机）：
- 正常路径：`lkm_window{calls=n, unload_reason=explicit}`，窗口时长 **不得**接近 watchdog；
- 崩溃路径：`kill -9` native 后 **fd 关闭 → 模块自动消失**（断言 `/dev/glk` 消失、`/proc/modules` 无残留）；
- 插件同步性：断言「最后一个插件返回」在「UNLOAD」之前（时间戳单调），且窗口内无并发调用（`calls` 单调 + 单开 `-EBUSY`）。

### 3.12.5 δ-3 真机事实（2026-10-05，门禁回填）

1. **节点权限不能靠 `miscdevice.mode`**：Android 的 `/dev` 由 **ueventd** 建节点，恒为 `0600 root`，且标签是通用 `u:object_r:device:s0`。
   → 必须**先 `misc_register` 再跑 UMH**，由 root 脚本（SELinux 尚 permissive）`chmod 666` + `chcon u:object_r:null_device:s0`。
   这是「不加 SELinux 放宽、不改 vendor 策略」的可行路径（已实测 shell 域可直接 open/ioctl）。
2. **终点等待上限 5s 过紧**：UMH 脚本要扫描 `/data/app` 找 ksud，冷缓存会擦过 5s 边界 → 生产/分阶段两处改为 **15s**。
3. **App 路径待验证**：`untrusted_app` 对 `null_device` 是否允许 `ioctl` 未验证；若被拒，需要单独标签决策（`root_cmd.sh` 已留 NOTE）。
4. **门禁清场是硬要求**：残留的常驻模块会让同名 `insmod` 失败（表现为「脚本没跑、标记没有」），门禁步骤必须 `UNLOAD`/`rmmod` + 清标记后再跑。

### 3.12.6 δ-4：插件 → LKM 端到端（已通，2026-10-05）

- **导出点唯一**：`plugin/host_ops.{hpp,cpp}` 把本次 run 的 `contract::Capabilities` 导成 `glk_contract_ops`；
  错误映射（可测）：成功 0 / `Unsupported` → `-EOPNOTSUPP` / `Unavailable` → `-EAGAIN` / `Faulted` → `-EIO` /
  `Rejected` → `-EINVAL` / `Closed` → `-EBADF`；`image_to_direct_map` 失败回 0（ABI 哨兵）。**绝不伪成功**。
- **窗口内同步派发**：`LkmWindowRuntime::run()` 对 `POST_TERMINAL` 的已注册 hook **一次一个同步调用**（§3.13 的唯一窗口内阶段）。
- **失败语义 = 对策 fail-soft**：窗口打不开或窗口体失败 → 记 `ChainResult::lkm_window_failed` 并**继续攻击**；
  插件侧观察 `Unsupported`；窗口的创建/关闭仍由链 `finish()` 唯一保证（无泄漏）。`LkmWindowFailed` 不再是中止原因。
- **端到端证据**（`device-gates/delta4-plugin-lkm-e2e-20261005-pass.md`）：真实 `.so` 插件 `glk.probe` 在窗口内
  `read_u64/read_bytes/write_bytes` 全 rc=0、越界读被内核拒（`bad_rejected=1`）；`lkm_window calls=4`（链自身 PING/UNLOAD 不计），
  即「插件真的经通道调用 LKM」的直接证据。

### δ 批逐文件清单

| 文件 | 动作 |
|---|---|
| `contract/abi/glk_contract_abi.h` | 增 `glk_lkm_*`（op/req/版本常量）；γ 批已把该头归位 |
| `tools/lkm/ghostlock/ghostlock.c` | 增 misc device + `ioctl` 实现（direct-map 校验、`UNLOAD`）；保留现有 `cmd/permissive` 语义 |
| `src/core/plugin/kernel_channel.{hpp,cpp}` | 新：`KernelMemory`/`KernelAlias` 的通道适配器 + `PING` 版本校验 |
| `src/core/backend/cve_2026_43284/steps/chain.cpp`（或 terminal） | 窗口包住：最后使用点后立即卸载（含回滚路径） |
| `src/core/tests/kernel_channel_test.cpp` | host：假 ioctl 后端，断言版本不匹配 fail-closed、`Closed` 后调用报错、地址越界 `-EFAULT` |
| `docs/analysis/device-gates/43284-lkm-channel-*.md` | 真机门禁：`lkm_window{calls=n}`、卸载后设备消失、AVB 校验 |

**门禁**：host / NDK 零告警 / lint 0 + **真机 43284**（窗口调用计数 + 卸载后 `/dev/glk` 不存在 + AVB）。

## 3.13 插件调用点在两条链上的锚定（设计）

**四条锚定原则**

1. **只在竞态窗口外**：PI 窗口在 `attack_write<M>` / route 内部；插件点必须落在其**之前或之后**，
   绝不插在窗口中（§3.8 规则 4）。
2. **尊重能力状态**：插件点能用的能力 = 该时刻状态为 `Available` 的能力（§3.6）；不够就 `NotAvailable` 报错。
3. **对策 fail-soft**：插件失败 = warning + 继续攻击（与现状一致，`steps.cpp:195`）；
   **攻击步骤不得 fail-soft**——两者的错误语义在 chain 层分开。
4. **LKM 驻留窗口内的插件点单独成类**：需要 `LkmProxy` 的对策只能在窗口内跑（§3.12.1）。

### 43499 链（执行顺序：W1 → slab_drain → 每轮[W2 spawn/leak → ancillary → W2 verify] → W3 → handoff）

| 插件阶段（CM 名） | 锚点（真实步骤） | 与窗口关系 | 该点可用能力（状态） | 现有/预期用户 |
|---|---|---|---|---|
| `PRE_ROUTE`（链首） | `W1` 之前（`W1W3Steps::run` 入口） | 窗口外 | Tier 1 `KernelMemory`（`write_zero`）可用；Tier 2 `NotAvailable` | —— |
| **`POST_SETUP`（= 现 `PreSpawn`）** | `W1` 完成、SELinux 已 permissive、victim 未 spawn（`steps.cpp:409-430`） | 窗口外 | `KernelAlias@Available`、Tier 1 `KernelMemory@Available`；`ChildTask@NotAvailable` | **`vr_guard`** |
| **`POST_SPAWN`（= 现 `PostSpawn`）** | `w2` 内：`child_task` 已知、**W2 verify 之前**（`steps.cpp:176-199`） | 窗口外（竞态线程空闲） | 上述 + `ChildTask@Available` | **`vr_task_tag`** |
| `PRE_ROUTE`（每次写前） | 每个 `attack_write<M>` 调用**之前**（`steps.cpp:100/282/293/346`） | **紧邻窗口**（窗口在其内部） | 同 `POST_SPAWN` | ——（预留给写前校验类对策） |
| `PRE_TERMINAL` | W3 完成、handoff 之前（`steps.cpp:464` 前） | 窗口外 | 已 arming 的全部能力 | 现 `PreHandoff` 枚举（未被 vivo 使用） |
| `POST_TERMINAL` | root child 完成（KernelSU 就绪）后 | —— | 若此时加载了 LKM → `LkmProxy@Available` | 预留 |

> 每轮 W2/W3 会**重复**触发 `POST_SPAWN`／`PRE_ROUTE`：对策插件必须**幂等**（现状 `vr_task_tag` 就是重试安全的）。

### 43284 链（`ChainStage`：ResolveTarget → ComputePlan → PatchCrashDump → Write → Verify → Hook → Trigger → WaitResult → Cleanup）

| 插件阶段 | 锚点 | 与窗口关系 | 该点可用能力（状态） | 备注 |
|---|---|---|---|---|
| `PRE_ROUTE`（链首） | `Idle → ResolveTarget` 之间 | 窗口外 | `FileCacheWrite@NotAvailable`（未 arming） | 只能做"链前准备"类对策 |
| `PRE_WRITE` | `Write` 之前（`PatchCrashDump` 之后） | 窗口外 | `FileCacheWrite`（arming 中） | 页缓存层对策 |
| `POST_WRITE` | `Verify` 之后、`Hook` 之前 | 窗口外 | `FileCacheWrite@Available` | 页缓存层对策 |
| `PRE_TERMINAL` | `Trigger` 之后、`WaitResult` 之前 | 窗口外 | `FileCacheWrite@Available` | —— |
| **`POST_TERMINAL`（内核态对策的唯一机会）** | `WaitResult` 成功后、`Cleanup` 之前（LKM 已加载、KernelSU 就绪） | 窗口外 | **`KernelMemory/LkmProxy@Available`**；`KernelHook@Available` | **LKM 驻留窗口内**；插件需在自卸载前完成 |
| `Cleanup`（终结点） | 链末 | —— | 关闭所有 arming 的能力 → `Closed` | 与「尽快自卸载」对齐：**先关能力，再卸载** |

### 由锚定得出的两条设计结论

1. **两条链的插件点集合不同但有共同骨架**：`PRE_ROUTE`(链首) / 关键写前后 / `PRE_TERMINAL` / `POST_TERMINAL`；
   43499 多一个 `POST_SPAWN`（有 victim/child task 概念），43284 多一个 `PRE_WRITE`/`POST_WRITE`（页缓存写前后）。
   → CM 阶段枚举应扩为：`PRE_ROUTE, POST_SETUP, PRE_WRITE, POST_WRITE, POST_SPAWN, PRE_TERMINAL, POST_TERMINAL`（7 个，原 5 个 + `POST_SETUP`/`PRE_WRITE`/`POST_WRITE`）。
2. **43284 的内核态对策略只能挂在 `POST_TERMINAL`**（LKM 驻留窗口内）——这直接回答了待确认 #12 的**调用点**问题：
   无论插件代码跑在 LKM 内还是 native 转发，**调用点都必须在窗口内**；窗口外声明 `KernelMemory` 需求的插件
   在**注册期就会被拒**（§6 + §3.12.1 规则 4）。

## 3.13.1 「阶段 × 能力状态」矩阵是**派生视图**，不是手写表

**回答**：**不要**为每个 backend 手写 7×M 的「阶段 × 能力状态」表——绝大多数格子是 `NotSupported`
或与上一阶段相同，手写必然腐烂且会与代码漂移。

**权威只声明三样（都很小）**

| 声明者 | 内容 | 规模 |
|---|---|---|
| **backend** | ① `declared()`：静态能力集（§3）② `stage_supported(PluginStage)`：**该 backend 有哪些阶段** | 每 backend ≈ 一行位集 + 一行阶段子集 |
| **step** | `arms()` / `closes()`：能力跃迁发生在哪一步 | 每步 0–2 项 |
| **plugin** | `requires()`（kind + 最低状态）+ 注册阶段 | 每插件 1–3 项 |

**矩阵由折叠派生**（与 §6 校验同一套算法）：以 `declared()` 为初始集，按 step 顺序应用 `arms/closes`，
在每个阶段锚点**采样**即得该阶段的能力状态。因此：

- **没有第二处真相** → 不可能漂移；
- **稀疏是自然的**：backend 没有的阶段直接不存在（`stage_supported` 为假 → 该阶段注册的插件在**注册期被拒**）；
- **可渲染**：host 测试/工具输出矩阵（如 `contract_matrix --dump`），供评审、文档与门禁引用；
  文档里的表是**生成物**，不是权威。

**派生矩阵示例（从上面的声明算出，非手写）**

43499（有阶段：`PRE_ROUTE`(首) / `POST_SETUP` / `POST_SPAWN` / `PRE_ROUTE`(每次写前) / `PRE_TERMINAL` / `POST_TERMINAL`；**无** `PRE_WRITE`/`POST_WRITE`）

| 能力 | 链首 | POST_SETUP | POST_SPAWN | 写前 | PRE_TERMINAL | POST_TERMINAL |
|---|---|---|---|---|---|---|
| `KernelMemory`（Tier 1 `write_zero`） | Available | Available | Available | Available | Available | Available |
| `KernelMemory`（Tier 2 `Fops`/`PipeBuffer`） | NotAvailable | NotAvailable | NotAvailable | NotAvailable | Available¹ | Available |
| `KernelAlias` | Available | Available | Available | Available | Available | Available |
| `AddressDiscovery` | Available | Available | Available | Available | Available | Available |
| `ChildTask` | NotAvailable | NotAvailable | **Available** | Available | Available | Available |
| `FileCacheWrite` | NotSupported | NotSupported | NotSupported | NotSupported | NotSupported | NotSupported |
| `LkmProxy` | NotAvailable | NotAvailable | NotAvailable | NotAvailable | NotAvailable | Available² |

¹ 前提是某步 `arms(KernelMemory/Tier2)`（C/KernelMemory 计划里由引导步建立通道）；
² 前提是 43499 的终态加载了我们的 LKM（§3.12 表）。

43284（有阶段：`PRE_ROUTE`/`PRE_WRITE`/`POST_WRITE`/`PRE_TERMINAL`/`POST_TERMINAL`；**无** `POST_SETUP`/`POST_SPAWN`）

| 能力 | 链首 | PRE_WRITE | POST_WRITE | PRE_TERMINAL | POST_TERMINAL |
|---|---|---|---|---|---|
| `FileCacheWrite` | NotAvailable | Available | Available | Available | **Closed**³ |
| `LkmProxy` / `KernelHook` | NotAvailable | NotAvailable | NotAvailable | NotAvailable | **Available**⁴ |
| 其余（`KernelMemory` 非 LKM、`AddressDiscovery`、`ChildTask`、`KernelAlias`） | NotSupported | … | … | … | NotSupported |

³ 链 en 关闭 arming 的能力（§3.13 结论：先关能力，再卸载）；
⁴ LKM 驻留窗口内；窗口关闭后（`Cleanup`）转 `Closed`。

**给实现的硬要求**：矩阵由 `contract_matrix`（host 测试 + 可选 CLI dump）生成并断言：
① 每个阶段采样值与折叠一致；② 没有「阶段支持但能力不可能 Available」的插件注册（注册期拒绝）；
③ 文档中的表**标注为生成物**（含生成命令与提交号），避免被当成权威手工维护。

## 3.14 插件自描述：运行时信息 + 参数 schema + wire/HOCON 大项（**新增需求 2026-10-05**）

需求原文：「plugin 应当可以获取接口提供的其他信息，例如**目前正在执行的 backend**，方便插件对不同后端动态适配；
plugin 同时要**注册 profile 结构**，配置文件和 wire 内加入 **plugin 大项**，由**统一管理的 HOCON 文件**向 plugin 传入外部参数」。

### 3.14.1 插件可查询的运行时信息（`RuntimeInfo`）

插件需要「我在哪个 backend/什么阶段/什么设备上跑」才能自适应。给它一个**只读**契约接口：

```cpp
/* contract/runtime_info.hpp —— 只读；无副作用；插件用它做动态适配 */
class RuntimeInfo : public CapabilityInterface {
public:
    [[nodiscard]] virtual BackendKind    backend()       const noexcept = 0;  // 当前执行的 backend（核心诉求）
    [[nodiscard]] virtual ChainId        chain()         const noexcept = 0;
    [[nodiscard]] virtual PluginStage    stage()         const noexcept = 0;
    [[nodiscard]] virtual std::string_view release()     const noexcept = 0;  // uname -r
    [[nodiscard]] virtual std::uint32_t  kmi()           const noexcept = 0;  // android_release*1000+minor
    [[nodiscard]] virtual std::string_view device_model()const noexcept = 0;  // 只读设备事实（可裁剪）
    [[nodiscard]] virtual CapabilityState state(CapabilityKind) const noexcept = 0;  // 能力状态（§3.6）
    /* 路径式查询：沿用既有 glk_host_ops.query_u64/query_str 语义（白名单路径） */
    virtual CapabilityResult<std::uint64_t> query_u64(std::string_view path) const noexcept = 0;
    virtual CapabilityResult<std::string_view> query_str(std::string_view path) const noexcept = 0;
    [[nodiscard]] CapabilityKind kind() const noexcept final { return CapabilityKind::RuntimeInfo; }
};
```

要点：**只读**（插件不得改这些）；新增 `CapabilityKind::RuntimeInfo`（不占 CM 的 7 位，属本树扩展）；
`query_*` 保留**路径白名单**（与现有 CM ABI 一致），避免变成任意读取口。

### 3.14.2 插件「注册 profile 结构」= 声明自己的参数 schema

插件不是只接一个字符串参数，而是**声明参数结构**，由统一 schema 体系统一处理（与 R1「单一 schema 权威」一致）：

```cpp
struct ParamSpec final {
    std::string_view name;        // "arm_delay_us"
    WireKind         type;        // uint | int | bool | string（与 §4.2 同源！）
    bool             required;
    std::optional<std::uint64_t> default_value;
    std::string_view doc;
};

struct PluginSpec final {         // 插件在加载期向宿主注册的元数据
    PluginId         id;          // 稳定标识，进 wire 路径：plugin.<id>.*
    std::uint32_t    abi_version;
    PluginStage      stage;       // 注册阶段（§3.13 的 7 个之一）
    CapabilitySet    required_caps;
    AccessDomain     domains;     // §3.5
    std::span<const ParamSpec> params;   // ← 参数结构（"注册 profile 结构"）
};
```

**收益**：插件参数**不再是自己解析的孤儿字符串**，而是进 §4.1 的 `FieldSpec` 体系 →
自动获得「类型校验 / 必填 / 默认值 / 诊断可见（R5）/ manifest 三端对拍」全部能力。

### 3.14.3 wire 与 HOCON 的 `plugin` 大项（owner-qualified）

```hocon
plugin {
  vivo_vr_guard {
    enabled = true
    stage   = "post_setup"           # §3.13 的 7 阶段之一
    module  = "plugins/vivo_vr_guard.so"
    params  { arm_delay_us = 200  retries = 5 }
  }
  samsung_defex {
    enabled = true
    stage   = "post_terminal"        # 只有在 LKM 驻留窗口内才有 KernelMemory
    params  { symbol = "task_defex_enforce" }
  }
}
```

对应 wire 路径（与 §4.2b 的 owner-qualified 规则一致）：
`plugin.<id>.enabled` / `.stage` / `.module_path` / `.module_hash` / `.required_caps` / `.params.<key>`。
```

**为什么放 `plugin` 大项而不是塞进 backend 段**：插件是**跨 backend 的设施**（同一对策可服务 43284 与 43499），
其配置必须与 backend 正交；但**插件能做什么**仍受「注册阶段 + 能力授予」约束（§3.8/§3.13），
所以它既不属于某个 backend，也不是能力本身——它是**第三类顶层项**。

### 3.14.4 统一管理的 HOCON

- **单一管理点**：`app/src/main/assets/kernel_profiles/plugin.conf`（随 App 资产版本化）；
  profile 文件可用 `include` 引入，形成「一个插件配置 → 所有设备 profile 共用」；
- **解析链**：`plugin.conf` → Kotlin 归一化（类型按插件注册的 `ParamSpec`）→ wire `plugin.*` 段 →
  native 绑定到插件加载器（`plugin/loader`）；
- **默认值权威**：参数默认值写在**插件注册的 `ParamSpec`**（不是 HOCON），HOCON 只写覆盖值 → 与 R1 一致；
- **可审计**：实际生效的插件与参数进入诊断（`plugin=<id> stage=<…> params={…}`），与 R5 同规。

### 3.14.5 CM ABI 增补（C 导出侧）

`glk_contract_ops` 需补：`backend_id` / `chain_id` / `stage` / `release` / `kmi`（或一个 `glk_runtime_info` 子结构），
以及模块侧的 `glk_module{ id, abi_version, stage, required_caps, domains, params_schema }`——
**仍由 `contract/` 导出**（§3.10：一份契约、两种投影），不另立第二份表。

### 3.14.6 插件制品与**四投影**（维护者 2026-10-05 明确）

插件是**外部 `.so`**：用户把 `.so` **导入 App**，App 按接口把它加载/接入攻击路径；同一份接口声明必须有
**native / Kotlin / extractor / wire(HOCON)** 四个投影，缺一不可。

| 投影 | 谁消费 | 职责 |
|---|---|---|
| **native C ABI** | `plugin/`（loader+registry+controller+host_ops） | `glk_entry(host_abi_version)` → `glk_module{ id, version, abi_version, required_caps, hooks[] }`；运行时按阶段同步调用，能力经 `glk_contract_ops` 注入（已有 δ-4） |
| **Kotlin** | App（导入/校验/配置 UI） | 识别插件、**校验插件特有配置**（按参数字段类型/必填/默认值）、生成 `plugin.<id>.*` 文档、管理启用/阶段/哈希固定 |
| **extractor** | `tools/extract_rs`（Rust） | 认识插件**特有的提取参数**（插件需要的符号/偏移/结构），在产出 profile 时解析/校验并写入 `plugin.<id>.extract.*` |
| **wire / HOCON** | 双方 | `plugin.<id>.{ enabled, stage, module_path, module_hash, params.*, extract.* }`（§3.14.3/§3.14.4） |

**导入与校验流程（安全关键）**
1. App 用文件选择器导入 `.so` → 复制到应用私有目录（`filesDir/plugins/<id>/<version>/`）→ 计算 **SHA-256** → 记录清单（id/version/abi/hash/caps/stages）；
2. **不在 JVM 内 dlopen 任意 `.so`**：Kotlin 通过 **native 探针**读取插件自描述（`--plugin-probe <path>` → 机器可读描述），
   由 Kotlin 拿到 `params_schema` / `extract_schema` 后渲染高级设置并做**类型/必填/默认值校验**（与 R1 的 `FieldSpec` 同源）；
3. 运行期：文档里带 `plugin.<id>.module_path` + `module_hash`，native 侧 loader 校验哈希后才 dlopen（现有的白名单+哈希机制复用）；
4. 可选 **sidecar 清单**（`<name>.json`）仅作离线预览；**权威是探针输出**，二者不一致时以探针为准并在诊断里报差异。

**extractor 特有参数（定义）**：插件声明的 `extract_schema` 描述「要 extractor 提供什么」——
如需要的符号名、结构偏移、或从 boot.img 解析的参数；extractor 用它**校验/补全** `plugin.<id>.extract.*`，
插件运行时通过 `RuntimeInfo`/参数读取，而不是硬编码在 `.so` 里。

**实现顺序（相对 S4）**：R1（schema 权威）→ R2（wire `plugin` 大项 + 生成式映射）→ **P1** native 探针 + Kotlin 导入/校验 UI → **P2** extractor 投影 → P3 参考插件（=CM-4）。

## 4. 能力接口（虚）

### 4.1 `KernelMemory`

```cpp
enum class MemoryChannel : std::uint8_t { Unavailable = 0, Fops, PipeBuffer /*, …全部机制 */ };
enum class CarrierKind   : std::uint8_t { Unavailable = 0, Ashmem, BinderDev, LoopControl /*, … */ };

class KernelMemory : public CapabilityInterface {
public:
    /* 原语（纯虚）：机制由调用方**显式**选择（enum 入参），不支持即 Unsupported。 */
    [[nodiscard]] virtual CapabilityResult<std::uint64_t>
    read(std::uint64_t address, std::span<std::byte> out, MemoryChannel) noexcept = 0;
    virtual CapabilityStatus
    write(std::uint64_t address, std::span<const std::byte> in, MemoryChannel) noexcept = 0;
    virtual CapabilityStatus
    write_zero(std::uint64_t address) noexcept = 0;          // Tier 1 引导（单字零写）

    [[nodiscard]] virtual bool supports(MemoryChannel) const noexcept = 0;
    [[nodiscard]] virtual bool supports(CarrierKind)  const noexcept = 0;

    /* 便利函数（**非虚**，由原语派生——NVI：公共 API 稳定，实现者只实现原语）： */
    [[nodiscard]] CapabilityResult<std::uint64_t> read64(std::uint64_t a, MemoryChannel ch) noexcept;
    CapabilityStatus write64(std::uint64_t a, std::uint64_t v, MemoryChannel ch) noexcept;

    /* 默认实现 = 非原子 RMW（读→改→写回）；实现可 override 更优版本。 */
    virtual CapabilityStatus update_bits(std::uint64_t a, std::uint64_t mask,
                                         std::uint64_t value, MemoryChannel ch) noexcept;

    [[nodiscard]] CapabilityKind kind() const noexcept final { return CapabilityKind::KernelWrite; }
protected:
    ~KernelMemory() override = default;      // 非拥有使用：不通过基类 delete
};
```

**读法**：`read/write/write_zero/supports` 是**纯虚**（backend 必实现）；`read64/write64` 是**非虚便利函数**；
`update_bits` 有**默认实现且可覆盖**（给合理默认，允许更快路径）。

### 4.2 其余能力（同构）

| 接口 | 头 | 纯虚原语 | 便利/默认实现 |
|---|---|---|---|
| `FileCacheWrite` | `file_cache.hpp` | `write16(file_offset, std::span<const std::byte,16>)` | `write_bytes`（多次 write16 拼接） |
| `AddressDiscovery` | `address_discovery.hpp` | `discover()` → `CapabilityResult<AddressDiscoveryResult>`（**失败不得写部分结果**） | —— |
| `KernelAlias` | `kernel_alias.hpp` | `to_direct_map(std::uint64_t image_addr)` | —— |
| `ChildTask` | `child_task.hpp` | `current()` → 当前 child task 句柄（每步刷新，不缓存） | —— |

## 5. 聚合 `Capabilities` 与生命周期

```cpp
/* 非拥有视图：backend 的实现对象必须**存活期 ≥ 链**（组合根持有）。 */
struct Capabilities final {
    KernelMemory     *kernel = nullptr;
    FileCacheWrite   *file_cache = nullptr;
    AddressDiscovery *address = nullptr;
    KernelAlias      *alias = nullptr;
    ChildTask        *child = nullptr;

    [[nodiscard]] CapabilitySet declared() const noexcept;          // 由非空指针推导
    [[nodiscard]] bool supports(CapabilityKind k) const noexcept;
};
```

- **可空 = 未提供**；调用前用 `supports()` 判定，或直接调用并处理 `Unsupported`（两者等价，后者更符合 R7）。
- **测试替身**：host 测试用 `FakeKernelMemory : public KernelMemory`（虚函数直接可 mock，比函数指针束自然——解禁后的直接收益）。
- **生命周期规则写进头注释**：`Capabilities` 不拥有实现；析构顺序 = 链 → 能力实现。

## 6. step 与 chain

```cpp
struct StepContext final { session::CoreSession &session; Capabilities &caps; };

enum class StepError : std::uint8_t { None, PreconditionFailed, CapabilityUnsupported, Failed, RollbackFailed };
struct StepResult final { StepError error = StepError::None; };

class Step {
public:
    virtual ~Step() = default;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual CapabilitySet requirements() const noexcept = 0;
    virtual StepResult prepare(StepContext &)  noexcept = 0;
    virtual StepResult run(StepContext &)      noexcept = 0;
    virtual StepResult rollback(StepContext &) noexcept = 0;   // 终结点恰一次由 chain 保证
};

enum class ChainId : std::uint8_t { Unknown = 0, Cve43499W1W2 = 1, Cve43499W1W3 = 2, Cve43284PageCacheWrite = 3 };

class Chain {
public:
    virtual ~Chain() = default;
    [[nodiscard]] virtual ChainId id() const noexcept = 0;
    [[nodiscard]] virtual std::span<Step *const> steps() const noexcept = 0;  // 组装结果（有序）
    [[nodiscard]] virtual CapabilitySet requirements() const noexcept = 0;     // 各 step 需求并集
    virtual StepResult run(ChainContext &) noexcept = 0;                       // prepare→run；失败逆序 rollback
};
```

**绑定规则（本次新增，硬性）：step 只能调用「它所属 backend」提供的能力**

```
chain 属于某个 backend（ChainId 的归属登记在 catalog）
  ① 静态：每个 step.requirements() ⊆ backend.declared()，且 step.domain() ⊆ backend.domains()
  ② 阶段：按 steps 顺序折叠能力状态（初始 = declared() 中真正可跃迁者），
     每步断言 requires ⊆ 当前 Available，随后并入 arms()  ← §3.6
  ③ 任何越权/越阶段调用：CapabilityError::Unsupported / Unavailable（fail-closed）
```

- **编译期**：`ChainSpec` 模板把「backend ↔ chain ↔ steps」绑成一体 + `constexpr` 折叠**能力状态机**（§3.6）→ 顺序不可行（如「读在 establish 之前」）直接编译失败；
- **注册期**：host 测试断言「每个已登记 chain 的静态需求 ⊆ 其 backend 声明」，且**阶段折叠**可行（无跨 backend 借能力、无越阶段调用）；
- **运行期**：越权调用返回 `CapabilityError::Unsupported`，未就绪调用返回 `Unavailable`（R7），都 fail-closed、不静默；
- **效果**：step 是**某 backend 的攻击链片段**，不是「通用插件」；跨 backend 共享只发生在**能力接口**层（如 `FileCacheWrite` 被 43284 与 43503 同时实现），而不是 step 直接挪用别的 backend。

**单一权威不变**：backend 只**提供** `Chain` 实现 + `CapabilitySet` 声明；
「哪个 (backend, chain, terminal) 可跑」仍由 `pipeline/component_catalog.hpp` 持有，
运行时通过 orchestrator 的一个 `switch` 取到具体 `Chain&`，编译期用 `Pipeline::target` `static_assert` 锁定。

## 7. 关键张力：**竞争窗口内的间接调用**

虚函数解禁后必须正视：**PI 竞争窗口**（`waiter→owner→consumer→CMP_REQUEUE_PI`）内的写原语若经虚表调用，
就多一次不可控的间接跳转。三个方案：

| 方案 | 做法 | 代价 |
|---|---|---|
| **(a) 全虚，接受窗口内间接调用** | 简单统一；按新政策由**真机门禁**验证 | 窗口内时序不可控；历史上正是这条促成了旧禁令 |
| **(b) 编排虚、窗口内编译期绑定（推荐）** | 能力接口用于**编排层**（步骤选择、能力查询、窗口外调用）；**窗口内那个写原语**由该 step 以**具体 backend 类型**做模板参数绑定（编译期直接调用） | step 需模板/双形态；契约仍统一，只有最内层保留具体类型 |
| **(c) 双入口** | 接口同时提供虚函数与 `final` 具体覆盖，热路径用后者 | API 面积翻倍，易误用 |

**建议 (b)**：「虚」用在**架构层**（契约、步骤、链、能力查询），「编译期绑定」留在**竞争窗口最内层**——
与「契约优先」不冲突：**契约规定能力语义，窗口内的实现绑定方式属于 backend 内部自由度**。

## 8. 与现有名字的对照（迁移表）

| 现在 | 目标 | 说明 |
|---|---|---|
| `contract::KernelMemoryOps`（函数指针束） | `KernelMemory`（虚接口） + `KernelMemoryOps`（**保留**，仅 C ABI/插件边界） | 见 §7 |
| `contract::FileCacheWriteOps` | `FileCacheWrite`（虚接口） | —— |
| `contract::AddressDiscoveryOps` | `AddressDiscovery`（虚接口） | 已活体接线（A3-2 ③），迁移时保持行为 |
| `concept AddressDiscovery`（既有） | **`AddressDiscoveryProvider`**（α 批已改名） | C++ 同命名空间内 class 与 concept **不能同名**；按「能力名=接口名」保留类名 |
| `contract::Capability`（CM 枚举） | `CountermeasureCapability` | 通用词汇叫 `CapabilityKind`；**不动 C ABI 位值** |
| `contract::StepSetKind` | `ChainId` | wire 数值 1/2/3 不变 |
| `Cve43499Primitives::attack_write<M>` | 保留为**窗口内编译期绑定**的写原语（§7b） | 它是 Tier 1 引导 |
| `backend/*/steps.cpp`、`steps/chain.cpp` | 逐步成为 `Step` 实现 + `Chain` 组装 | 见 `contract-first-chain-plan.md` |

## 9. 硬约束

1. `-fno-rtti`、静态 libc++：**虚函数与 `-fno-rtti` 不冲突**（不用 `dynamic_cast`/`typeid`）。
2. 虚接口**只用于编排与能力调用**；**竞争窗口内避免间接调用**（§7(b)）。
3. 契约层**可 host 编译**，且 R1 防火墙下不得 include `backend/`/`pipeline/`/`terminal/`。
4. 每个 `CapabilityError` 必须能映射到诊断输出（`GLK_STATUS`/`pr_status` 风格）。
5. **未支持 = 错误**（R7）；任何「返回 0 / 静默返回」都是缺陷。
6. 析构：接口析构函数 `protected`（非拥有使用），实现类 `final`，生命周期由组合根持有。

## 10. 裁决状态（2026-10-05：维护者指示「开工」→ 全部按本文建议默认通过）

| # | 议题 | 裁决 |
|---|---|---|
| 1 | 竞争窗口 | **§7(b)**：编排虚、PI 窗口内编译期绑定 |
| 2 | 便利函数 | **NVI**：`read64/write64` 非虚（由原语派生）；`update_bits` 有默认实现可覆盖 |
| 3 | 聚合与生命周期 | `Capabilities` **非拥有指针** + `CapabilityInterface` 标记基类 |
| 4 | 插件边界形态 | `*Ops` 函数指针束**仅用于 C ABI/插件边界**（`glk_contract_abi.h`） |
| 5 | 访问域 | 接口按**地址种类分型**（`KernelMemory` / `KernelAlias` / `FileCacheWrite`），`AccessDomain` 仅用于声明与诊断 |
| 6 | 绑定规则 | step 需求 ⊆ 所属 backend 声明集（编译期 + 注册期双检） |
| 7 | 状态机 | `{NotSupported, NotAvailable, Available, Closed}` + `requires/arms/closes` + `constexpr` 折叠 + **显式比较表** |
| 8 | 文件页缓存读 | **并入 `FileCacheWrite`**（读-改-写协议同生命周期） |
| 9 | 阶段枚举 | **扩到 7 个**：`PRE_ROUTE, POST_SETUP, PRE_WRITE, POST_WRITE, POST_SPAWN, PRE_TERMINAL, POST_TERMINAL` |
| 10 | 内核态插件执行位置 | **native 经通道转发**进 LKM（复用 `plugin/` 加载器与 `glk_contract_ops`）；调用点固定在 LKM 驻留窗口内 |
| — | LKM 通道 | 完整 `KernelMemory` ops、**不设授权门**、**尽快自卸载**为一等约束（§3.12/§3.12.1） |
| — | 命名 | `countermeasure`+`ancillary` → **`plugin/`**；ABI 头 → `contract/abi/glk_contract_abi.h`；`glk_cm_*` → `glk_*` |

## 11. 实施批次与 α 批逐文件清单

| 批 | 内容 | 触及 | 门禁 |
|---|---|---|---|
| **α** | **契约词汇 + 接口骨架**（纯新增，零行为改动，无人消费） | `contract/*` 新头 + 测试 + Makefile | host / NDK 零告警 / lint |
| **β** | `Capabilities` 进 `CoreSession` + 退役 `AncillaryOps` + `steps.cpp` Tier1 适配器 | `session`/`ancillary`/`steps.cpp` | host/NDK/lint + **真机 43499** |
| **γ** | ABI 归一与改名：`contract/abi/glk_contract_abi.h`、`glk_*` 前缀、`plugin/` 目录 | `contract`/`platform`/`ancillary` 全量重命名 | host/NDK/lint + 三端对拍 |
| **δ** | LKM 版本化通道 + `MemoryChannel::LkmProxy` 适配器 + 窗口约束 | `tools/lkm/**` + `plugin/` | host/NDK/lint + **真机 43284**（含自卸载断言） |

### α 批逐文件清单

**新增**
- `src/core/contract/capability.hpp`：`CapabilityKind`（并集，低 7 位 `static_assert` 对齐 `GLK_CM_CAP_*`）、
  `CapabilityError`（含 `Closed`）、`CapabilityState`（四态 + `satisfies` 显式表）、
  `CapabilityResult<T>`、`CapabilitySet`、`CapabilityInterface`
- `src/core/contract/kernel_memory.hpp`：`MemoryChannel`/`CarrierKind` 枚举、`KernelMemory` 虚接口
  （纯虚 `read/write/write_zero/supports/state/establish/close` + NVI `read64/write64` + 可覆盖 `update_bits`）、
  `KernelMemoryOps`（**原样迁入**，C 边界用）
- `src/core/contract/file_cache.hpp`：`FileCacheWrite` 虚接口 + `FileCacheWriteOps`（原样迁入）
- `src/core/contract/kernel_alias.hpp`：`KernelAlias` 虚接口
- `src/core/contract/child_task.hpp`：`ChildTask` 虚接口
- `src/core/tests/contract_vocabulary_test.cpp`：位对齐 static_assert、`satisfies` 真值表、四态跃迁、
  `CapabilitySet` 语义、`Ops` 仍 trivially-copyable/standard-layout

**修改**
- `src/core/contract/capabilities.hpp`：退化为 umbrella（`#include` 上述新头 + `Capabilities` 聚合 + `declared()`）；
  **保持既有类型名与布局不变**（`KernelMemoryOps`/`FileCacheWriteOps` 定义逐字保留，避免任何行为/布局变化）
- `src/core/contract/address_discovery.hpp`：仅新增 `AddressDiscovery` 虚接口（`AddressDiscoveryOps` 与既有测试不动）
- `src/Makefile`：`HDRS` 增新头、`NATIVE_HOST_TESTS` 增 `contract_vocabulary_test`

**不变量（α 批）**：无任何生产 TU 消费新接口；`steps.cpp`/`pagecache`/终端一行不改；
既有 `capabilities.hpp` 消费者（`pagecache.cpp`、`support/util.cpp`）**编译与行为不变**。

## 12. 原始待确认（留档）

1. **§7 方案选 (b)**（编排虚、窗口内编译期绑定）——认可？
2. **便利函数用 NVI**（`read64/write64` 非虚、`update_bits` 有默认实现可覆盖）——认可？
3. **`Capabilities` 用非拥有指针 + `CapabilityInterface` 标记基类**（便于测试替身与注册表）——认可？
4. **保留 `*Ops` 束仅用于 C ABI/插件边界**（`glk_cm_abi.h` 是 C）——认可？
5. **访问域建模（§3.5）**：接口按「地址种类」分型（`KernelMemory` 内核 VA / `KernelAlias` 翻译 / `FileCacheWrite` 文件偏移），域枚举只用于声明与诊断——认可？还是希望**统一成一个** `ArbitraryAccess(domain, address)` 接口？
6. **绑定规则（§6）**：step 的需求必须 ⊆ 其所属 backend 的声明集（编译期 + 注册期双检）——认可？
7. **能力状态机（§3.6）**：`CapabilityState{NotSupported, NotAvailable, Available, Closed}` + 每机制状态查询 + `establish/close` 跃迁；
   step 声明 `requires`（kind + 最低状态）与 `arms`（我把谁变 Available）；链级 **constexpr 折叠**做阶段校验——认可？
   （若认可，`CapabilityError` 就**由状态派生**，不再另立判断源。）
