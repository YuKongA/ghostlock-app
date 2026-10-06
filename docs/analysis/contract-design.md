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

> **现状注（2026-10-05）**：本节的阶段表是 **R6b 之前的推演**，已被 **§3.14.7.8 的按-backend 核实结论**取代——43499 只有 `pre_terminal` 可用（`pre_spawn`/`post_spawn`/`post_terminal` 声明但不可用），43284 只有 `post_terminal` 可用（仅 LKM 驻留窗口内）。下表保留作历史；引用时以 §3.14.7.8 与设计 `plugin-runtime-integration-design.md` §12 为准。

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
| ~~**`POST_SETUP`（= 现 `PreSpawn`）**~~ **43499 不可用**（窗口按写尝试开关，无「窗口关闭且 child 未建立」的点；§3.14.7.8） | ~~`W1` 完成、SELinux 已 permissive、victim 未 spawn（`steps.cpp:409-430`）~~ | —— | —— | 已废止 |
| ~~**`POST_SPAWN`（= 现 `PostSpawn`）**~~ **43499 不可用**（child 建立后窗口仍会为后续写尝试重开；§3.14.7.8） | ~~`w2` 内：`child_task` 已知、**W2 verify 之前**（`steps.cpp:176-199`）~~ | —— | —— | 已废止 |
| `PRE_ROUTE`（每次写前） | 每个 `attack_write<M>` 调用**之前**（`steps.cpp:100/282/293/346`） | **紧邻窗口**（窗口在其内部） | 同 `POST_SPAWN` | ——（预留给写前校验类对策） |
| `PRE_TERMINAL` | W3 完成、handoff 之前（`steps.cpp:464` 前） | 窗口外 | 已 arming 的全部能力 | 现 `PreHandoff` 枚举（未被 vivo 使用） |
| ~~`POST_TERMINAL`~~ | **43499 不可用**（root 接管后控制流不回宿主；§3.14.7.8） | —— | —— | 已废止 |

> 每轮 W2/W3 会**重复**触发 `POST_SPAWN`／`PRE_ROUTE`：对策插件必须**幂等**（现状 `vr_task_tag` 就是重试安全的）。

### 43284 链（`ChainStage`：ResolveTarget → ComputePlan → PatchCrashDump → Write → Verify → Hook → Trigger → WaitResult → Cleanup）

| 插件阶段 | 锚点 | 与窗口关系 | 该点可用能力（状态） | 备注 |
|---|---|---|---|---|
| `PRE_ROUTE`（链首） | `Idle → ResolveTarget` 之间 | 窗口外 | `FileCacheWrite@NotAvailable`（未 arming） | 只能做"链前准备"类对策 |
| `PRE_WRITE` | `Write` 之前（`PatchCrashDump` 之后） | 窗口外 | `FileCacheWrite`（arming 中） | 页缓存层对策 |
| `POST_WRITE` | `Verify` 之后、`Hook` 之前 | 窗口外 | `FileCacheWrite@Available` | 页缓存层对策 |
| `PRE_TERMINAL` | `Trigger` 之后、`WaitResult` 之前 | 窗口外 | `FileCacheWrite@Available` | —— |
| **`POST_TERMINAL`（内核态对策的唯一机会）** | `WaitResult` 成功后、`Cleanup` 之前（LKM 已加载、KernelSU 就绪） | **窗口内**（LKM 驻留） | **`KernelMemory/LkmProxy@Available`**；`KernelHook@Available` | **LKM 驻留窗口内**；插件需在自卸载前完成 |
| `Cleanup`（终结点） | 链末 | —— | 关闭所有 arming 的能力 → `Closed` | 与「尽快自卸载」对齐：**先关能力，再卸载** |

### 由锚定得出的两条设计结论

1. ~~两条链的插件点集合不同但有共同骨架~~ → **（已被 §3.14.7.8 取代）**：按代码核实，每个 backend **只有一个**可用插入点——43499 = `pre_terminal`，43284 = `post_terminal`（LKM 驻留窗口内）；词汇表固定四词，可用性按 backend 表达，**不再扩阶段枚举**。
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

**为什么放 `plugin` 大项而不是塞进 backend 段**：插件是**跨 backend 的设施**（同一对策可服务 43284 与 43499），
其配置必须与 backend 正交；但**插件能做什么**仍受「注册阶段 + 能力授予」约束（§3.8/§3.13），
所以它既不属于某个 backend，也不是能力本身——它是**第三类顶层项**。

### 3.14.4 统一管理的 HOCON

- **单一管理点**：`app/src/main/assets/profile/plugin.conf`（随 App 资产版本化）；
  profile 文件可用 `include` 引入，形成「一个插件配置 → 所有设备 profile 共用」；
  **现状注（2026-10-05）：该资产已取消**——插件配置属设备/用户特有，改走**覆盖存储**；见 §3.14.7.5；
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

**P2 已落地（2026-10-05，`593e51de`）**：extractor 新增 `--plugin-descriptor <probe-stdout.tsv>`（**可重复**、**仅 `--format conf`**）——严格解析 §3.14.7.2 的 7 列 TSV → `PluginDescriptor`；取值解析顺序 **R1**（读回正在产出的 profile 字面量，限已发射路径）/ **R3**（BTF `struct.<s>.<f>` 偏移或 `sizeof.<s>`）/ **R2**（kallsyms 符号名，基址相对）。**写入者 = extractor**：输出 `plugin { <id> { extract { … } } }`——**仅 `extract`**、是**可导入片段**而非 wire 文档、追加在 `countermeasure` 之后、无条目时输出**逐字节不变**；`required` 无法解析 = 硬错误，`optional` 缺失 = 省略（**default 不顶替**），声明类型与解析值矛盾 = 硬错误；HOCON 键简单 token 裸写、含点/特殊字符加引号（R1 的 owner-qualified 键**始终加引号**，与 App `HoconSupport.keyName` 一致）。**P1 只校验形状**：键名与类型来自描述符，不由 P1 决定。

**实现顺序（相对 S4）**：R1（schema 权威）→ R2（wire `plugin` 大项 + 生成式映射）→ **P1** native 探针 + Kotlin 导入/校验 UI → **P2** extractor 投影（**已落地 `593e51de`**）→ **P3 参考插件**（**已移出为独立项目 `ghostlock-plugin-example`**——插件作者不需要 exploit 仓库；本仓库只留 `tools/plugins/README.md` 指引与宿主侧 `src/core/plugin/**`；**CM-4 的 Vivo 对策仍未完成**）。

### 3.14.7 P1 接口冻结（2026-10-05）

**冻结范围**：P1 落实 §3.14.6 四投影中的 **native + Kotlin** 两投影——探针 CLI、探针 stdout 描述、C ABI 尾部追加、`countermeasures/` 根、`plugin.*` wire 字段。
**extractor 投影 = P2**（`extract_schema` / `plugin.<id>.extract.*`），**参考插件 = P3**（独立项目 `ghostlock-plugin-example`，内置 ABI 头 + `build.sh android|host|abi-check`；CM-4 的 Vivo 对策仍待真机）。
本节条文自 2026-10-05 起**冻结**：`native-core` 与 `kotlin-app` 按此编码；需要偏离时先改本节（= Lead 裁决）再改代码。
本节未冻结的细节只有一处：`RuntimeInfo`（见 3.14.7.3 末条）。

#### 3.14.7.1 探针 CLI

```
ghostlock --plugin-probe <path.so> [--expect-sha256 <hex>]
```

- 独立入口 `support::cli::Mode::PluginProbe`，与 `--ghostlock-app-call` / `--load-prebuilt-profile` / `--probe-cve-2026-43284` **互斥**（多入口 = 参数错误，fail-closed）；
- **不读 profile、不读 stdin**，不需要 root；只读描述后 **`dlclose` 即退出**，**不注册 hook、不运行 hook**、不保留 Fd 或映射；
- 独立进程运行，启动即 `prctl(PR_SET_NO_NEW_PRIVS, 1)`；
- `--expect-sha256` 给定时先对文件算 SHA-256：不一致 → 拒绝（`reject` 行 + 非 0 退出码），且**不进 `dlopen`**；
- 退出码：0 = 正常产出描述；非 0 = 路径 / 哈希 / ABI / 描述错误（原因进 `reject` 行与 stderr）。

#### 3.14.7.2 stdout 描述格式（TSV）

stdout **只**输出 TSV（诊断走 stderr）。**描述行的第一列是 `kind`；列序（TAB 分隔）为冻结值**——P1 起不得增删或改序，扩展只能整列追加并同批改本节：

| kind | 列序（TAB 分隔） |
|---|---|
| `plugin` | `kind, id, version, abi_version, size, sha256, stages, required_caps` |
| `hook` | `kind, id, trigger, stage, priority, name` |
| `param` | `kind, id, name, type, required, default, doc` |
| `extract` | `kind, id, name, type, required, default, doc` |
| `reject` | `kind, id, reason` |

- `id`：该行所属插件的稳定 id（`reject` 行写被拒插件 id；路径不可解析时写 `-`）；
- `stages` / `required_caps` 为**逗号分隔**列表，空写 `-`（「空 = 全集」语义由 `stage_mask` / host 侧判定，不由本格式推断）；
- `default` 空写 `-`；`type` ∈ `uint | int | bool | str`，与 GLKv3 `WireKind` **同字面量**（两侧同一份对拍）；
- `required` 为 `0 | 1`；`priority` 为十进制整数；`size` 为十进制字节数；`sha256` 为小写 hex；
- **空值一律写 `-`**；
- header 行块：**必须位于描述行之前**（首列是**键名**而不是 kind），且**至少**含两个**必填键**——
  `host_abi`（= `GLK_ABI_VERSION`）、`countermeasures_root`（= **相对目录名 `countermeasures`**，不是绝对路径）；
- 其余 header 键**可选但类型化**：`host_stages`（host 实现的 stage 列表）、`host_caps`（host 实现的 capability 位）、
  `stage_availability`（按 backend 的阶段可用性，见下）——**出现即校验**，缺省不影响解析；
- **未知 header 键 → 拒绝**（fail-closed）；「2 必填 + 3 可选」的放宽理由是**前向兼容**：四键旧 golden 仍可解析（已实践验证），收紧没有安全收益；
- `stage_availability` 语法：`<backend>:<stage>[,<stage>];<backend>:<stage>[;…]`；`<backend>` 用**短 token**（`43499` / `43284`），`<stage>` 取 `pre_spawn|post_spawn|pre_terminal|post_terminal`；当前冻结字面量：`stage_availability\t43499:pre_terminal;43284:post_terminal`；
- **唯一权威 = native `src/core/plugin/schema.hpp` 的 `RuntimeBackend` + `stage_available_on()`**（探针 header 与 host 注册校验都从它生成）；**Kotlin 不得硬编码该矩阵**（违反按 F4 的「禁硬编码」处理）；
- **缺该行 = 无可用阶段**（保守置灰）；未知 `<backend>` / `<stage>` token → **fail-closed**；
- `countermeasures_root` 取相对目录名的理由：**环境无关**——探针进程的 `GHOSTLOCK_HOME` 与 App 的 `filesDir` 未必相同，绝对路径无法对拍；App 侧**精确匹配该字面量 `countermeasures`**；`-` 仅表示探针无 home、不可校验；
- **行结束**：行以 `\n` 或 `\r\n` 结束；**孤立的 `\r` 不是分隔符**（不得当换行处理）；
- 解析规则：首列命中 kind 词汇即为描述行，其前为 header 行（`键<TAB>值`）——该区分规则为冻结语义；
- 对拍测试（native ↔ Kotlin agreement test）按上表逐列断言。

**必需列、退出码与探针/注册期一致（2026-10-05 冻结）**

- **必需列**（`param` / `extract` 的 `name`、`hook` 的 `name`）**不得为空、不得为 `-`**；出现 `-` 即视为**缺失**，**消费侧必须拒绝**；
- **产出端不得发行必需列缺失的行**：无法描述时只发 `reject` 行，不发行半成品描述行；
- **退出码语义**：**无法描述**（哈希不匹配 / ABI 不兼容 / 入口缺失 / 打开失败）→ **非 0** + `reject` 行；**可描述但不可用**（注册期会被拒，如能力不足、阶段在该 backend 不可用）→ **0** + `reject` 行；消费侧以 `reject` 行为准判定 unusable；
- **探针与注册期口径一致（D7）**：探针须镜像 loader 的可见判定集——`hook.name` ≤ 64、name 无控制字符、caps 位已知、trigger/stage 词表——使「探针通过 = 注册期通过」。

**三端一致性与共享语料**

- 审计结论（2026-10-05）：三端（`plugin/probe.cpp` 产出 / Kotlin 消费 / Rust 消费）逐条约 **30 条规则中 24 条一致**，10 条分歧已分类立项——**D1/D2/D3/D4** 缺陷、**D5/D7/D8** 硬化、**D6** 放宽、**D9/D10** 文档/注释；
- **共享语料**：`app/src/test/resources/plugin-probe-conformance/{accept,reject}/*.tsv`（**目录即结论**：`accept/` 必须被接受、`reject/` 必须被拒），配**两侧 walker**（Kotlin / Rust）；walker 必须**自证文件集合完备**（枚举目录，不得只跑白名单）；
- **语料取值域规则**：`accept/` 用例的每个取值必须落在 **ABI 声明域之内**——`plugin.size` / `host_abi` / `plugin.abi_version` / `hook.priority` 为 **uint32**（`src/core/contract/abi/glk_contract_abi.h` 的 `uint32_t size;` / `uint32_t abi_version;` / `uint32_t priority;`；探针按 uint32 打印，`plugin/probe.cpp:375`(size) / `:373`(abi_version) / `:358`(priority)），`required` ∈ {0,1}，bool 默认 ∈ {0,1}，列表项须为词表内 token。**越界值属「生产者不可能产出」的输入**：不得作为 `accept/` 用例（否则等于要求消费端接受一个现实中不存在的值）；若语义上应被拒，放 `reject/`。

行形状示意（`→` 仅表示 TAB；列序即上表冻结值）：

```
host_abi→1
countermeasures_root→countermeasures
host_stages→pre_spawn,post_spawn,pre_terminal,post_terminal
host_caps→kernel_read,kernel_write,alias,child_task
stage_availability→43499:pre_terminal;43284:post_terminal
plugin→<id>→<version>→1→<size>→<sha256>→<stages>→<required_caps>
hook→<id>→<trigger>→<stage>→<priority>→<name>
param→<id>→<name>→uint→1→200→<doc>
extract→<id>→<name>→str→0→-→<doc>
reject→<id>→<reason>
```

#### 3.14.7.3 C ABI：只允许尾部追加（**不 bump `GLK_ABI_VERSION`**）

- 新增 `glk_param_type`（`uint | int | bool | str`，枚举值与 GLKv3 `WireKind` 同源）与 `glk_param`（name / type / required / default_value / doc）；
- `glk_module` **尾部追加**：`param_count` / `params` / `extract_count` / `extract` / `stage_mask`；既有 7 个字段（abi_version / size / name / version / required_caps / hook_count / hooks）的顺序、含义与数值**冻结**（见 `contract/abi/glk_contract_abi.h` 的 append-only 约定）；
- **size 门控读取**：host 按 `module->size` 判断尾部字段是否存在；v1 模块（size = v1 结构大小）的尾部字段视为 0/NULL，**不得解引用**；v2 头给出 `GLK_MODULE_SIZE_V2 = sizeof(glk_module)`；
- **不 bump `GLK_ABI_VERSION`**（保持 1）：追加字段对旧 host / 旧模块都是安全忽略；`abi_version` 只表达**不兼容**变化，新增能力用「声明 + 忽略」而非版本号；
- extract 条目在 P1 与 `glk_param` 同构（name / type / required / doc）；P2 若需扩展，按同一「尾部追加」规则处理；
- `RuntimeInfo`（§3.14.1）**不属于 P1**：留待 P3，避免把「当前 backend / stage」这类运行时值提前塞进静态描述。

#### 3.14.7.4 常量与目录：唯一权威

- 插件根目录 = `<GHOSTLOCK_HOME>/countermeasures`（`default_countermeasure_dir()`，`plugin/loader.cpp:166`），P1 起为唯一权威；
- `plugin.<id>.module_path` 是**相对 `<GHOSTLOCK_HOME>/countermeasures`** 的路径，形如 `<id>/<version>/<file>.so`：加载器对非绝对路径做 `target = whitelist_dir_ + "/" + target`（`plugin/loader.cpp:296`；`whitelist_dir_` = `default_countermeasure_dir()`）。**绝对路径不属于 P1 契约**；
- 探针 header 的 `countermeasures_root` 报告**相对目录名 `countermeasures`**（不是绝对路径）：探针进程与 App 的 home 未必相同，绝对路径无法对拍；
- 该字面量与拼接规则**必须**同时出现在探针 header 与 Kotlin agreement test 中并对拍，不允许第二份手写常量。

#### 3.14.7.5 wire / HOCON

- 路径冻结：`plugin.<id>.enabled` / `.stage` / `.module_path` / `.module_hash` / `.params.<key>` / `.extract.<key>`（§3.14.3 / §3.14.4）；
- **native 先行**：先落 `FieldSpec` 与 `profile-manifest-v3.tsv` 的 `plugin.*` 字段（类型 / 必填 / 默认值），Kotlin 只按 manifest 生成映射（沿用 R2 生成式映射与 F4 禁硬编码机制）；
- **`plugin.conf` 资产取消（裁决 2026-10-05）**：插件配置属**设备/用户特有**，走既有**覆盖存储**（导入流程与高级设置写入用户配置），**不加资产、不改 67 个资产的 `include`（含 `index.conf`）**；`ProfileLayout` 的键白名单接受 `plugin.<id>.*` 并 **fail-closed**（未声明前缀 / 未识别键仍拒绝）；§3.14.4 的「统一管理点 + include」表述由本条取代；
- `module_path` / `module_hash` **不进资产**（由导入流程写入用户配置）；`module_path` 的相对基准见 §3.14.7.4（`<GHOSTLOCK_HOME>/countermeasures`）；
- **默认关闭**：`enabled` 缺省 false；只有 `enabled = true` 时 Kotlin 才把该插件的 `plugin.<id>.*` 写进文档，未启用插件不得出现在 wire 里。

**跨语言形状（canonical，2026-10-05 修复）**：`plugin` 是**段名**，其键是**平铺的 `<id>.<field>`**（如 `plugin` 段下的 `vivo_vr_guard.enabled`）。

- owner 段白名单只认**精确 `plugin`**；`plugin.<id>` 这种「把 id 当段名」的形状在**解码期即 fail-closed**；
- `plugin::validate_plugin_wire` **已接进生产路径**：解码之后、任何 backend 绑定之前执行；失败打印 `plugin configuration rejected: <Name> id=<id>` 并终止（fail-closed，不静默忽略）——见 `src/core/main.cpp:129-143`；
- **缺陷记录**：此前三方形状不一致——白名单按 `plugin.` **前缀**、校验器按**裸 `plugin`**、Kotlin 发射 **`plugin.<id>` 段**；双方**各自测各自的形状**，因此都没发现。修复：`1df00435`（Kotlin 改为 `plugin` 段 + `<id>.<field>` 平铺键 + `enabled` bool）与 `e5792ead`（白名单精确 `plugin`、校验进生产、探针 header 加 `stage_availability`）；
- **判据（物证）**：`app/src/test/resources/plugin-wire-shape-golden.bin`（**2069 B**，由 App 编码器产出，Kotlin 在 `PluginEmissionDocumentTest` 中逐字节自断言）；native 侧将以 host 测试断言「同一字节流经 `parse` + `validate_plugin_wire` 被接受」。跨语言形状**以该 golden 为物证**——只对拍各自形状不足以发现前缀/段名偏差。

#### 3.14.7.6 切分与顺序（硬约束）

1. **native 半场**：探针 CLI + TSV 输出 + ABI 尾部追加 + `plugin.*` FieldSpec/manifest + wire 绑定（含 fail-closed 校验）；
2. **Kotlin 半场**：导入（文件选择器 → 私有目录 → SHA-256 清单）→ 经探针解析自描述 → 按 schema 校验 → UI 高级设置 → 只写启用项的 `plugin.<id>.*`；
3. 顺序不可交换：manifest 是 Kotlin 映射的权威，Kotlin 不得先于 native 半场硬编码字段；
4. **门禁**：host / NDK / lint / Kotlin 测试 + 真机（delta4 试验台，外加 App 内导入一次——需维护者操作）；真机结果按 `docs/analysis/device-gates/` 归档。

#### 3.14.7.7 动态键声明（`params.*` / `extract.*`）

**背景**：`plugin.<id>.params.<key>` / `.extract.<key>` 的键与类型由插件自身的 `ParamSpec` 决定（探针 TSV 的 `param` / `extract` 行，§3.14.7.2），静态 manifest 无法逐条枚举；而 `NativeProfileGlkv3Adapter` 只认 manifest 里的精确 path。

**裁决（A）：动态声明进 native `FieldSpec` / manifest（单一权威）**

1. manifest 增**两条通配行**：`plugin.<id>.params.<key>` 与 `plugin.<id>.extract.<key>`，`type` 列写**联合字面量 `uint|int|bool|str`**——这是**唯一允许的联合**，且成员集合必须**恰好等于**这四个（由测试钉死）；`doc` 列 params 行写「Dynamic: type comes from the plugin descriptor (probe TSV param rows)」，extract 行写 P2 语义；
2. **语法扩展**：manifest 的 `type` 列允许 `|` 分隔的**联合**（成员取自既有 wire kind 词表）；Kotlin adapter 按联合解析，**未知成员 fail-closed**；
3. **native 绑定**：`params.*` / `extract.*` 按前缀接受，只做「值类型 ∈ 联合集合」检查；**键是否存在、required、类型是否与描述符一致** 在插件实例化 / 门禁阶段按探针描述符 fail-closed 校验；
4. **通配必须显式存在**：任一侧都不得写隐式前缀规则；其它 owner 前缀仍严格 fail-closed（未声明的路径一律拒绝）；
5. **对拍测试**：用探针 fixture 的 `param` / `extract` 行做正例（描述符内的键全接受）与负例（描述符外的键拒绝）；
6. **按 backend 的阶段可用性与 hook 级拒绝**见 §3.14.7.8：某个 hook 的 stage 在该 backend 不可用 → **拒绝该 hook**（不拒绝整个插件）、记账并给出 `StageUnavailableOnBackend`；全部 hook 被拒则记 `no_usable_hooks=1`；
7. **`extract.*` 的写入者是 extractor**（P2 `--plugin-descriptor`，R1/R3/R2，仅写 `extract` 片段，见 §3.14.6）；**P1 只校验形状**；HOCON 中插件 id 与含点键**加引号**；
8. **`param` / `extract` 行的必需列 `name`** 非空且不得为 `-`（必需列语义见 §3.14.7.2）；产出端不得发行缺必需列的行；动态键解析的三端判据是共享语料 `app/src/test/resources/plugin-probe-conformance/{accept,reject}/`。

**静态四行**（`plugin` 为第三类顶层 owner，`<id>` 是路径占位符）：
`plugin.<id>.enabled`（bool，默认 `literal:0` = 关闭）/ `.stage`（str，host stage token）/
`.module_path`（str，**相对 `<GHOSTLOCK_HOME>/countermeasures`**，不含额外一层）/ `.module_hash`（str，64 位小写 hex）。

**绑定 / 校验规则**：`enabled` bool；`stage` ∈ host stage token 集合；`module_path` 相对路径、无 `..`、非绝对；`module_hash` 64 位小写 hex；`params.*` / `extract.*` 键非空、类型按**描述符**校验；**未启用的插件不得出现在 wire 文档里**（§3.14.7.5）。

**实现出处（回填，2026-10-05）**
- **动态行**：`src/core/plugin/schema.hpp` 的 `kPluginGlkv3Fields` 以 `profile::glkv3::WireType::Union` 声明 `plugin.<id>.params.*` / `.extract.*`；清单拼写 `uint|int|bool|str` 即 `wire_type_name(WireType::Union)`，联合成员表 `kUnionScalarTypes` **恰好 4 个**（`src/core/profile/glkv3.hpp`）；commit `4d25d6e7`；`profile-manifest-v3.tsv` 字段数 **103 → 109**（plugin 行 6 条，其中 union 2 条，L114–L119）；
- **绑定与门禁**：`src/core/plugin/wire.{hpp,cpp}` —— `validate_plugin_wire()` 遍历文档 `plugin` 段，`PluginWireError` 覆盖 `UnknownField` / `EnabledMissing` / `EnabledNotBool` / `DisabledPresent` / `StageMissing` / `StageUnknown` / `ModulePathRejected` / `ModuleHashRejected` / `ParamKeyRejected` / `ParamTypeRejected` / `TooManyPlugins`；前缀匹配的**唯一入口**是 `plugin_dynamic_key()`（`schema.hpp`，无隐式前缀规则）；`kMaxPluginsPerDocument = 16`，容量不足报 `TooManyPlugins` 而非静默丢弃；
- **描述符门禁**：`plugin_descriptor_declares(module, kind, name, type_out)` 按 **size 门控**读取——v1 模块没有尾部字段，因此声明为空（拒绝）；
- **Kotlin**：`NativeProfileGlkv3Adapter` 解析 manifest 的 `|` 联合（未知成员 fail-closed）、按 `<id>` 占位行解析具体路径（**无隐式前缀规则**），并以 `declaredTypeNames()` / `declaredWire()` 暴露声明；commit `0b1c0fa0`。

**判据**
- **native**：`src/core/tests/plugin_wire_test.cpp` —— `std::size(kUnionScalarTypes) == 4` 且联合拼写 `uint|int|bool|str`；`kPluginGlkv3Fields` 6 行中恰 2 条 union；`plugin_dynamic_key` matcher 矩阵（params / extract / 空 key / 前导点 / 非 plugin 段）；**11 例文档校验**（none + good 两例通过，其余命中上述错误名）；`plugin_descriptor_declares` 的 **v1-size 拒绝**；
- **Kotlin**：`PluginProbeGoldenTest`（设备 golden 硬断言）+ `PluginProbeTest` / `PluginConfigValidatorTest` / `PluginManifestTest`；
- **设备 golden**：`app/src/test/resources/plugin-probe-golden.tsv`（commit `7d54ce78`，**11 行、逐字节来自设备探针 stdout**，覆盖 4 种 param 类型 + `hook` 行 + 多值 `stages`/`caps`；消费方 `app/src/test/kotlin/com/ghostlock/app/data/plugin/PluginProbeGoldenTest.kt`），完整 stdout 见 `docs/analysis/device-gates/s4-p1-probe-20261005-pass.md` §5。

#### 3.14.7.8 阶段可用性（按 backend 核实，取代一切旧推演）

**结论**：四词词汇表（`pre_spawn` / `post_spawn` / `pre_terminal` / `post_terminal`）保持不变，**可用性按 backend 表达**——每个 backend **只有一个**可用插入点，其余阶段是「**声明但不可用**」（注册期拒绝，不是沉默缺席）：

| backend | 可用 | 声明但不可用 |
|---|---|---|
| `cve_2026_43499` | **`pre_terminal`**（`steps.cpp:484-490` / `:517-521`） | `pre_spawn` / `post_spawn` / `post_terminal` |
| `cve_2026_43284` | **`post_terminal`**（`lkm_window.cpp:99-107`，仅在 LKM 驻留窗口内） | `pre_spawn` / `post_spawn` / `pre_terminal` |

**依据（代码为准；设计 `docs/analysis/plugin-runtime-integration-design.md` §12，commit `ab0561f8`）**：

- **43499 的 race 窗口按「写尝试」开关**：`steps.cpp:118` 的 `Cve43499Primitives::attack_write<M>(...)` 位于每次写尝试的循环内（`:100-130`），`userspace_clean` 由 route 在**同一次调用内**置位（`route/multicast_waiter_route.cpp:53/71/86/133`、`route/select_stack_route.cpp:125`、`route/tcp_zerocopy_route.cpp:95`），而 victim/child 就在该写循环里建立（`steps.cpp:480` 的 w2 victim round）。因此**不存在「窗口已关闭且 child 未建立」的点**：`pre_spawn` 与 `post_spawn` 不可用（child 建立后仍会有后续写尝试再次开窗）；
- **43499 `pre_terminal`**：`steps.cpp:484-490`（W1W3：w3 之后、`return StageResult::Continue` 之前）/ `:517-521`（W1W2 同形），随后 `cve_2026_43499_backend.cpp:136` 检查结果 → `:138-158` 移交 rooted child；该点**窗口已关闭**（最后一次 `attack_write` 已返回）且内核写能力已就绪；
- **43284 `post_terminal`**：`lkm_window.cpp:99-107`（注释明示「POST_TERMINAL is the one stage inside the LKM residency window」，`registry_->dispatch(..., CountermeasureStage::PostTerminal, &host_ops_)`）——唯一同时具备内核能力的窗口；43284 没有 waiter/spawn 概念，`pre_spawn`/`post_spawn` 不适用，`pre_terminal` 无内核特权（按「声明但不可用」处理）；
- 探针 header 的 `host_stages` 如实列出 host 实现的 stage，**`stage_availability`（header 第 5 行）按 backend 导出本矩阵**（唯一权威 `schema.hpp::stage_available_on()`，语法见 §3.14.7.2）；host 在 `open()` 时按 backend 校验可用集合，只注册不可用阶段的插件必须对作者可见，不得靠沉默缺席表达；
- **hook 级拒绝语义（与矩阵配套，host 实现）**：hook 的 stage ∉ 所选 backend 的可用集 → **拒绝该 hook**（**不是**拒绝整个插件）+ 记账 + 命名原因 **`StageUnavailableOnBackend`**；因此一个模块可以同时声明「43284 `post_terminal` + 43499 `pre_terminal`」，在两侧各自只剩可用 hook；若某插件的**全部** hook 都被拒 → 记 `no_usable_hooks=1`（fail-soft；建议不加载以避免无意义映射）。host 的注册校验直接调用 `stage_available_on()`，**不得复制矩阵字面量**。

**已废弃的原假设（2026-10-05）**：本文早先写过「`pre_spawn` 不在 Pipeline 层，而是 `w1()` 成功之后、`w2()` 之前」——该假设是按 Pipeline 边界**推测**的，已被上面的「按写尝试开关的窗口」**推翻**：`w1()` 与 `w2()` 之间并不存在满足前置条件的插入点。随之，原先记的「`run<Route>` 模板体内行号待钉死」一项**关闭**（问题不再存在）。

**与 §3.14.6 的关系**：P1 = native + Kotlin 两投影的落地；extractor 投影 = P2；参考插件 = P3（独立项目 `ghostlock-plugin-example`）。

**UML**：本节是设计冻结，尚无落地结构；P1 落地后按 AGENTS.md 同批刷新 `docs/development/full-process-uml.md` 的 §3.1 / §3.2 / §3.3（新增插件类与关系）。

#### 3.14.7.9 能力位 `log` 与插件日志契约（**设计框架，待 native 设计稿定稿**）

> **⏸ 工程冻结（用户指令 2026-10-05）**：本节的**运行时使用**随插件工程暂停并禁用（App **不再发射** `plugin.*`；native **字面注释掉**宿主接线（构造/打开/派发/卸载——**不是开关**；代码/测试保留，**恢复需撤销注释**）——**native 侧注释已提交 = `ca968a5a`**，**App 侧（停止发射/隐藏入口）在工作树、待提交**）；**契约与已落地物证保留**，恢复条件 = **新架构完成 + 用户放行**。
> **状态**（2026-10-05）：① **A 批（43284 全链日志）已落地**（`a04bdb5b`；真机门禁 PASS `device-gates/43284-logging-20261005-pass.md`，正例 16 行 `run.43284`）；② **B 批（能力位）产出端已落地**（`2c9457fe`「batch B producer - GLK_CAP_LOG capability bit with a single capability catalog, per-module ops copy with quota/rate-limited host logging and log_calls/log_dropped accounting」）：`GLK_CAP_LOG = 1u<<7`、`Capability::Log`、**单一能力目录 `kCapabilityCatalog`**（`caps_list()` 改为遍历它，新位不会从 `host_caps` 列静默消失）、host 每模块栈上 ops 拷贝 + 前缀/截断/配额/限速 + `log_calls`/`log_dropped`；**B 批已全线落地**：产出端 `2c9457fe`（`GLK_CAP_LOG=1u<<7` + **唯一能力表 `kCapabilityCatalog[8]`** + `caps_list()` 遍历 + host 每模块 ops 拷贝/配额/限速/计数）、**Rust 消费端 `b7eb5f95`**（cap 词表接受 `log` 并被测试钉住；示例插件优先 `ops->log`、老 host 回退 stderr 同形前缀）、**Kotlin 消费端 `fe66c793`**（token 透传 + 未知未来 cap 直通断言；fixture 第 4 行加 `log`；真机采集物字节级复制为第二输入 `plugin-probe-glk-probe-device.tsv` + `PluginProbeDeviceCaptureTest`）——**影响面表已逐行标「已同步」**。**剩余仅两项设备侧待办（设备可用后）**：① **新示例插件产物 `97c50d4d…` 上机后重采真机 golden**（模块哈希行更新）；② **三张 UI 截图**（Lead / `kotlin-i18n` 拍）；**两项裁决见本节末**。用户要求「插件接口提供内置日志接口」；**ABI 与 host 已具备该入口**，缺的是**能力位**与词表登记（见下表）。**本节不含实现代码**。

**现状（代码事实）**

| # | 事实 | 依据 |
|---|---|---|
| 1 | ABI 里**已有**日志入口：`void (*log)(void *ctx, int32_t level, const char *msg);` | `src/core/contract/abi/glk_contract_abi.h:142`（`glk_contract_ops`） |
| 2 | host **已实现**：`ops.log = &log_thunk` | `src/core/plugin/host_ops.cpp:215` |
| 3 | loader 侧默认实现：`ops.log = &default_log` | `src/core/plugin/loader.cpp:162` |
| 4 | **能力位没有 `LOG`**：`glk_capability` = `KERNEL_READ(1<<0)`…`KERNEL_HOOK(1<<6)`，共 7 个 | `glk_contract_abi.h:103-110`；C++ 侧 `contract::Capability` + `capability_token()`（`contract/countermeasure.hpp:293-306`） |
| 5 | 探针 `host_caps` 由 `kHostImplementedCaps` 生成、**不列 `log`**；插件 `required_caps` 含未知位即 `caps_rejected` | `src/core/plugin/probe.cpp:277-290`（`caps_list` 白名单 `:109-122`）；`contract/countermeasure.hpp:89` 与静态断言 `:254-258` |
| 6 | **43284 全链目前 0 处 `pr_*`**：可观测输出只有 LKM 窗口诊断串 `lkm_window opened=…` | 本批 grep（`src/core/backend/cve_2026_43284/**` 计 0）；`src/core/backend/cve_2026_43284/lkm_window.cpp:17` |
| 7 | 用户参照：上游 `V4bel/dirtyfrag` 的 `exp.c`（1952 行 / **63 处日志**，以错误路径与粗粒度进度为主），要求我们**展示更多**日志 | 用户需求 + Lead 的按文件统计（外部事实） |

**契约（B 批；设计稿已定稿、两项裁决已下——A 批已落地，B 批待放行）**

1. **能力位**：`glk_capability` **尾部追加** `GLK_CAP_LOG = 1u << 7`（**append-only，不 bump `GLK_ABI_VERSION`**，沿用 §3.14.7.3 纪律）；词表 token 建议 `log`（由 `capability_token()` 与探针 `caps_list` 同步）；
2. **语义（设计稿 r1 定稿值）**：
   - **level 词表**：`0=error / 1=warn / 2=info / 3=debug`，**越界按 1 处理并记 `level_clamped`**；
   - **msg**：纯文本、**无格式串语义**，**上限 256 B**（超出截断并追加 `…`）；
   - **配额/限速**：**每模块每 run 64 条**、**≥1 条/ms**（`CLOCK_MONOTONIC`），超限**丢弃并计数**（`log_dropped`，不静默）；
   - **host 前缀**：`[countermeasure] <id> log(<level>): <msg>` → **stderr**（未知 id 降级为 `-`；现状缺 `<id>`，本次补上）；
   - **路由**：只进运行日志 + 诊断块，**不进 `run_state`**；失败/被拒**绝不改变控制流**（fail-soft）；
   - **按模块归因**：host 为每个模块构造栈上 `glk_contract_ops` 拷贝（`log` 换成该模块 thunk，其余 9 个转发），**不信任插件自报 id**——配额/限速/归因都必须按模块；
   - **记账**：`HostDiagnostics` 新增 **`log_calls` / `log_dropped`**（additive，`run.plugin host` 行加这两个字段）；
3. **为什么是能力位而不是隐式约定**：插件必须能在**注册期 fail-closed 地检测** host 是否支持日志——`required_caps` 含 `log` 而 host 无该位 ⇒ 探针 `caps_rejected` + host 注册拒绝；隐性约定无法检测、只能靠试错，且与既有 7 个能力位的做法不一致。

**影响面清单（词汇变更必须全列）**

| 面 | 影响 | 处置顺序 | 状态 |
|---|---|---|---|
| native 产出端 | `glk_capability` + `contract::Capability` + `capability_token()` + **唯一能力表 `kCapabilityCatalog[8]`** + 探针 `caps_list()` **遍历该表**（新位不会静默消失） | **先行** | ✅ **已同步**（`2c9457fe`） |
| 探针 golden（**仓库 fixture**） | `plugin-probe-golden.tsv` **第 4 行 `host_caps` 加 `log`**（**格式增量**，其余列数/列序不变）；**身份行是合成 fixture，不重采** | 产出端之后 | ✅ **已同步**（`fe66c793`，Kotlin 侧） |
| 真机采集物（**证据**，非 fixture） | `build/gate-logs/plugin-probe-golden-device.tsv`（sha256 `1a6e49d8c015e7d0…`）**字节级复制**为第二测试输入 `app/src/test/resources/plugin-probe-glk-probe-device.tsv`（sha 一致）+ 新 `PluginProbeDeviceCaptureTest` | 采集即归档 | ✅ **已同步**（`fe66c793`）；**重采待办**见状态块（新示例插件产物 `97c50d4d…`） |
| 共享语料 | `plugin-probe-conformance/{accept,reject}` **63 份不需改**（输入子集仍合法）；**walker 参考串**随能力表更新 | 随 fixture 同批 | ✅ **已同步**（`b7eb5f95`，Rust 侧） |
| Rust 消费端 | `tools/extract_rs`：cap 词表**接受 `log` 并被测试钉住**；示例插件优先 `ops->log`、老 host **回退 stderr 同形前缀** | 产出端之后 | ✅ **已同步**（`b7eb5f95`） |
| Kotlin 消费端 | `PluginProbe` 确认为 **token 透传**（**无词表校验**）⇒ 补断言钉住「`log` 直通 + **未知未来 cap 也直通**」；fixture 第 4 行；真机采集物作第二输入 | 产出端之后 | ✅ **已同步**（`fe66c793`） |
| 文档 | 本节 + UML（§3.1 能力位枚举与探针输出）+ branch-plan 条目 | 同批 | ✅ **已同步** |

> **物证职责边界（必须分清）**：**golden 的用途是跨端解析一致性**，其**身份行是合成数据**（真机不可能复现），因此它的更新是**格式增量**而非「设备重采」；**真实制品的一致性由真机采集物 + 门禁日志承担**（`build/gate-logs/plugin-probe-golden-device.tsv` 一类）。两者**不可互相替代**，也不得混为一句「重采 golden」。
>

> **边界（本次确立）**：**native 新增能力位不需要 Kotlin 改动**——Kotlin 侧对 `host_caps` 只做**透传与展示**（`fe66c793` 用断言钉住「`log` 直通 + **未知未来 cap 也直通**」）。凡「Kotlin 词表必须同步」的旧说法**作废**；Kotlin 只在**需要展示名**时补 UI 文案。
>
> **示例 / 参考实现（取舍，理由）**：示例插件**不把 `GLK_CAP_LOG` 声明为必需**——该位语义是「**要求 host 具备日志能力**」，声明会让**老 host 拒收**整个模块；示例改为**优先 `ops->log`、老 host 回退 stderr 同形前缀**（真机 golden 的 `plugin` 行 `required_caps` 亦保持 `kernel_read,kernel_write`，`b7eb5f95`）。要「强制要求日志能力」的插件才应声明该位。
>
> **禁止**：消费端先行，或「两边各自加词」——先加词表、后加 fixture/golden 的顺序会产出**双方各自能过、合起来不过**的假绿（P1 形状缺陷的同类教训，见 §3.14.7.5）。

**附：A 批（43284 全链日志）与本节的边界**——**已落地**：`a04bdb5b`「batch A logging - bounded structured run.43284 lines across the whole chain, named failure reasons, keys withheld; 16 lines on the happy path」；载体 = `backend/cve_2026_43284/diag_line.hpp` 的 `DiagLine`（固定缓冲、无分配、无格式串、值内控制字符归一 `_`、**绝不含密钥**）；结构行 `run.43284 <phase> k=v` 单行 ≤256 B（超出截断并追加 `truncated=1`），失败路径每条具名原因 + 上下文；**真机门禁 PASS**（`device-gates/43284-logging-20261005-pass.md`：五例退出码 0、正例 16 行、负例 B 不中断链、无插件回归 `run.plugin` 0 行且 43284 链日志逐行同形）。**A 批无 wire/ABI 变更**，本节的 ABI/词表变更只属 **B 批**；**本批只做 43284，43499 另排**（43499 已有多处 `pr_*`，统一另批）。

**裁决已下（Lead 2026-10-05，设计稿 §E 两项均已裁定）**：① **A 批 verbosity = always-on** ✓——**不引入** `backend.cve_2026_43284.log_verbosity`（用户抱怨的就是「默认太少」，opt-in 等于没解决；保持「纯 backend、无三端同步」；有界 ≤120 行/run、单行 ≤256 B 即不会刷屏；将来要降噪再另批三端改动）；② **B 批配额照准** ✓：**每模块每 run 64 条、单条 ≤256 B、≥1 ms/条**，超限静默丢弃并计 `log_dropped`，**不改控制流**。

## 3.15 `payload` 顶层 owner 契约（设计已定稿；实现待 native 半场）

> **状态**：设计终稿 r2（`docs/analysis/terminal-payload-tiers-design.md`，commit `c335aabc`）+ **用户已确认**；**本仓库尚无实现**，契约先冻结。**状态回填约定**：① payload 实现批次（native 半场）落地时、② root 管理器 P1 放行时（§3.15.8），各回填一次「实现状态 + 依赖检查」——两处现在都是「设计定稿待实现」。
>
> **⏸ 工程冻结（用户指令 2026-10-05）**：payload / 自定义 handoff **暂停并暂时禁用**——按用户指令：**`payload` owner 不再被接受**，App **不再发射** `payload.*` 且隐藏入口（**App 侧在工作树、待提交**；**native 侧字面注释已提交 = `ca968a5a`**）；**执行半场、step 3b、handoff 设计稿推进**均停止；**本节契约与已落地实现/测试保留**（可逆）；恢复条件 = **新架构完成 + 用户放行**。
> 依据：payload 设计 §9 的 9 条裁决；跨切面沿用 `plugin-extract-spec-design.md` §9.3（唯一命名空间）/ §9.4（诊断记实际命中）/ §9.5（分发顺序 = 准入）的做法。

### 3.15.1 定位与段名

- `payload` 是**第四类顶层 owner**（`common` / `backend.<id>` / `platform.*` / `countermeasure.*` / 精确 `plugin` 之后），**与 `terminal` 正交**：terminal 决定「如何接管」，payload 决定「接管之后做什么」；
- **不新增** combination token、不动 `kCombinationCatalog`（避免组合目录按档数炸开）；
- 缺 `payload.*` ⇒ **文档逐字节不变**，行为与今天完全一致（默认关闭）。

### 3.15.2 字段与拼写（r2 冻结）

| wire 路径 | 类型 | 规则 |
|---|---|---|
| `payload.tier` | str | ∈ {`exec`, `script`, `ko`}；缺失/为空 ⇒ payload 不启用 |
| `payload.exec.command` | str | **argv 形式**，≤256 B；**永不 shell 拼接** |
| `payload.exec.sha256` | str | 可选；64 位小写 hex |
| `payload.script.path` | str | 相对 `<GHOSTLOCK_HOME>`，≤256 B |
| `payload.script.sha256` | str | 可选；64 位小写 hex |
| `payload.ko.count` | uint | 1..8 |
| `payload.ko.<i>.path` | str | `i` 十进制且 `0 ≤ i < count`；路径规则同上 |
| `payload.ko.<i>.sha256` | str | 可选；64 位小写 hex |

**fail-closed（绑定期）**：`count` 缺失/0/>8；实际 `ko.<i>.path` 个数 ≠ `count`；出现 `i ≥ count`；`i` 非十进制或重复；任一 `path` 绝对 / 含 `..` / 反斜杠 / NUL / 超长；`sha256` 存在但不是 64 位小写 hex；**单档互斥违反**（`tier=exec` 时出现 `script.*`/`ko.*`，反之亦然）⇒ **拒绝整份文档**。

> **用户决定（2026-10-05，App 侧简化）**：**App 的 payload 页不再录入哈希**（原话：「**不要校验哈希，应当假设用户知道他们传入了什么**」）。`payload.*.sha256` 的**定义与校验语义保留**——**native 有则校验、无则接受**；字段保留给**自动化 / 将来使用**（App 不再产生它，也不因它缺失而拦截）。

### 3.15.2.1 manifest `required` 列与索引键拼写（裁决 2026-10-05）

**A. `required` 列 = 「无条件必需」**（澄清；此处曾被期望表带偏，按物证纠正）

- **物证（按符号引用，不按行号——行号已漂过一次）**：`src/core/profile/schema.hpp` 的 **`kPayloadGlkv3Fields`** 中**只有 `payload.tier` 行是 `true`**，其余 **7 行全为 `false`**；
- **档内条件性**（`tier=exec ⇒ exec.command`；`tier=script ⇒ script.path`；`tier=ko ⇒ ko.count`）**由校验器**保证：`profile/glkv3_parse.cpp` 的 **`validate_payload_section`**；`kPayloadGlkv3Fields` 上方注释亦写明这些行的用途是「让两份 manifest 与 Kotlin adapter 能区分**已声明的 payload 路径**与普通键」；
- **规则**：**`required` 表达「无条件必需」，不表达「选中该档时必需」**；**档内条件性属校验器/契约语义，不得写进 manifest 的 `required` 列**。

**B. 索引键最终拼写 = `ko.<i>.path` / `ko.<i>.sha256`**（裁决：**改代码对齐文档**；当前无任何发射方 ⇒ **零迁移成本**）

- **最终 wire 拼写（唯一权威）**：`payload.tier` / `payload.exec.{command,sha256}` / `payload.script.{path,sha256}` / `payload.ko.count` / **`payload.ko.<i>.{path,sha256}`**（`0 ≤ i < count ≤ 8`）；
- **旧拼写 `payload.<i>.path`（无 `ko.` 前缀）现被 fail-closed 拒绝**（**负例**）——它在实现里曾与 `ko.count` 自洽但**与设计文档不一致**，且会产生「`ko.count` 在 `ko.` 下、索引却在顶层」的怪状；
- **角括号占位符约定**：`section` + `<占位符>` 的**后缀匹配**，与 `plugin.<id>.params.*` **同规**（`plugin_dynamic_key()` 即此形态）；Kotlin 侧 `declarationFor()`（`profile-core/src/main/kotlin/com/ghostlock/app/data/profile/NativeProfileGlkv3Adapter.kt:126`）**当前只对 `plugin.` 前缀做后缀匹配**，**具体索引路径的匹配分支留给 batch (b)**；
- **⏸ 状态：随 payload 冻结失效（沿革保留）**—— ① **原实现曾在工作树落地但未提交**（`kPayloadGlkv3Fields` 用 `ko.<i>.path` / `ko.<i>.sha256`、校验器**要求 `ko.` 前缀**并拒绝裸 `<i>.path`、两份 manifest 重生成且逐字节一致，工作树 sha256 `018804b363583612…`；**已提交的 `74db3594` 是旧拼写那次**——归因只写实际包含该改动的那次提交，故本节**不写提交号**）；② **冻结后 `payload` 段出现即拒（fail-closed）** ⇒ **`ko.<i>` 对齐当前无运行时消费者**（`kPayloadGlkv3Fields`、校验分支、manifest 8 行、相关测试与 Makefile 目标均已**字面注释**）；③ **恢复＝撤销注释 + 跑门禁**（恢复清单见 `branch-plan.md` 冻结清单与 `task-9`）；④ 上文原始裁决与拼写规则**保留不删**（沿革）；
- **剩余（Kotlin 侧）**：`declarationFor()` 的**索引路径匹配分支**仍留给 batch (b)——Lead ping `kotlin-i18n` **重钉**其测试后，本节再补一句「Kotlin 已同步」。
### 3.15.3 安全边界与授权面

| 维度 | 规则 |
|---|---|
| 路径 | 一律相对 `<GHOSTLOCK_HOME>`；禁绝对 / `..` / 反斜杠 / NUL；**realpath 二次校验**；≤256 B（形状可复用 `plugin_module_path_valid()`，根不同） |
| 哈希钉 | `sha256` 给定时，**执行 / 加载之前**逐字节比对（`support::sha256_file`）；不符 ⇒ fail-closed **且不执行**——**native 侧语义**；**App 页不再录入**（见 §3.15.2 用户决定） |
| 大小 | 先 `stat` 大小上限、**再读入**（ko 建议 ≤64 MiB，与 43499 module 上限同量级） |
| ko 内容 | **必须**过 `lkm::precheck_module_file`（ELF / vermagic / `__versions` / 签名）；**不得**因「用户自定义」放宽 |
| argv vs 脚本 | `exec.command` 是 argv（native 按空白切分后以 argv 传递，不做变量 / 通配展开）；`script.path` 指向**用户自带的脚本文件**——**命令不是脚本**，禁止把命令文本当脚本执行 |
| 授权面（App） | **无授权步骤**（用户决定 2026-10-05：「**无需授权**」）——不设确认门、不设可撤销开关。**界面不再有摘要行**（用户决定 2026-10-05：「**本次将：以内核权限运行 xxx 也删掉**」，**覆盖先前「保留摘要」的裁决**）；**但「运行日志行」保留**——英文、进运行日志，与「界面摘要」是**两件事**，禁止混为一谈（界面上没有告知行 ≠ 日志里不记本次将执行什么）。文案：`以 LKM 执行脚本` → **`以内核权限执行脚本`**、`内核扩展（.ko）` → **`向内核注入内核扩展`**（**文案以 App 资源为准**，本地化须**自然中文、不做逐字直译**） |
| 检查栏（App） | **不再有独立检查栏**（用户决定：「**无需下面的检查栏**」）；**运行期校验仍在**：**native 是权威**，运行前只在**运行按钮附近提示阻断原因**（不另起一栏） |
| 清除按钮 | **删除**（用户决定：「**无需清除按钮**」）——切到**默认档**（**`启动 root 管理器`**，见 §3.15.8）即等价清空（**只发射当前档**，不存在需要显式清除的残留） |
| 不可信内容 | 用户提供的一切不可信：结果只记账，**不放宽任何既有校验** |

**风险记录（用户决定 2026-10-05 后仍成立）**：去掉哈希录入、授权步骤与**界面摘要行** ⇒ **用户对 payload 内容自担责任**；native 侧仍 **fail-closed 校验字段与路径**（含「若给了 `sha256` 则比对」）；**可见告知只剩运行日志行**（英文，进运行日志），界面不再提示本次将执行什么。

### 3.15.4 失败语义（硬边界）

- **payload 失败绝不中断攻击链**（waiter / race / handoff 不受影响）；
- 用户**显式请求**了 payload 而未完成 ⇒ 本次运行结论标 **「未完成」** + 逐项原因 `payload_error=<reason>` / `ko[i]=<reason>`；**提权记录照记**（`uid0` / KernelSU ready）——既不谎报全成功，也不让已完成的提权白费；
- `ko` **逐项执行**：第 i 个失败记 `ko[i]=<reason>` 后**继续下一个**，**不整体回滚**；全部成功 ⇒ payload 完成；
- 结果只影响结论与诊断，**不**改变控制流、不放松任何检查。

### 3.15.5 可用性矩阵（terminal × tier；native 导出）

| terminal | `exec` | `script` | `ko` |
|---|---|---|---|
| `root_child` | 可用 | 不可用（无 LKM 通道） | 可用（经 root child late-load） |
| `umh_forward` | 可用 | 可用 | 可用 |

- 绑定期校验：`(terminal, tier)` **不在矩阵内 ⇒ fail-closed 拒绝**；
- 沿用 `stage_availability` 的纪律：**native 导出矩阵**（后续可加进词汇 manifest / 探针 header），**Kotlin 不硬编码**，UI 用同一矩阵置灰并给原因；
- 矩阵以**代码实际能力**为准，实现批次逐项核实后钉死。

### 3.15.6 结构同步（同批，强制）

payload 是**新顶层 owner** ⇒ 同批更新：
- `docs/development/full-process-uml.md`：§3.1 的 owner 段与 schema（`ghostlock__payload`）、§3.2 的 Kotlin layout / UI 组、§1 IPO 的 C 段（校验 → 绑定 → 接管后执行）；
- `AGENTS.md` 的顶层 owner 白名单表述（`common` / `backend.` / `platform.` / `countermeasure.` / 精确 `plugin` / **新增 `payload`**）；
- 提交信息写明更新了哪几张图。

### 3.15.7 门禁要求（实现批次）

- **host**：编码 / 解码矩阵（tier 非法、单档互斥违反、`count` 与实到不符、`i ≥ count`、重复 / 非十进制 i、超长路径、控制字符、sha256 非 64hex）→ 全部 fail-closed；矩阵外 `(terminal,tier)` → 拒绝；**无 `payload.*` ⇒ 逐字节回归**；
- **NDK 零告警 + lint 0 + Kotlin 测试**；
- **真机**（攻击关键路径门槛）：三档**正例**（`exec` rc=0 / `script` 标记文件 / 2 个 ko 一好一坏 → 坏项记 `ko[1]=<reason>`、攻击链 PASS 但本次运行标「未完成」）+ **负例**（sha256 不符 / 路径含 `..` / 超限 / 矩阵外组合 → 拒绝且**不执行**）+ **无 payload 回归**；AVB 12/0；结果按 `device-gates/` 归档。

### 3.15.8 `payload.root.*`（**设计稿，待用户确认**——占位）

> 详细设计：`docs/analysis/root-manager-selection-design.md`（L 级设计稿 v1）。本节只登记**契约占位**，形态待用户答复后定稿；**不含实现**。

- **定位**：默认档（启动 root 管理器）是 payload 轴的一种 ⇒ `payload.tier = "root"`，与 `exec`/`script`/`ko` **单档互斥**；
- **wire 路径（草案 v2）**：`payload.root.kind` ∈ {`kernelsu`, `folkpatch`, `custom`}、`payload.root.manager`（**可选的管理器包名**，精确选择；缺省 = 代码权威 `me.weishu.kernelsu`；用户手填即其自己的声明）、`payload.root.argv`（**仅 `custom` 必填**，≤192 B = `contract::RootProgram::kArgvCapacity`）；
- **fail-closed（草案 v2）**：`kind` 不在白名单；`custom` 缺 `argv`；`argv` 含控制字节或超长；非 `custom` 却出现 `argv`；`manager` 非合法包名（非空、`[A-Za-z0-9_.]`、≤128 B）；**管理器不存在 / 不可启动**（App 预检只是提前提示，**权威判定在 native 绑定前复核**，fail-closed + 具名原因，**不降级、不猜替代品**）；`payload.root.*` 与 `tier != "root"` 同时出现 ⇒ 拒绝整份文档；
- **argv vs 模块**：`kernelsu` 走既有 `ksud late-load`（无 shell、无拼接）；`folkpatch` 走 **KernelPatch 模块加载**（`kernelpatch.ko` 由用户提供/导入，**软重启**为独立显式确认动作）——两者机制不同，不得互相顶替；
- **向后兼容**：**无 `payload` 段 = 今天的行为（KernelSU/ksud）逐字节不变**；**显式 `kernelsu` 与不写等价**；
- **UI 分期（本批，用户决定 2026-10-05）：默认档 = 「启动 root 管理器」+ 子菜单** —— **语义档**：`payload.tier` 的**默认**不再是「不自定义」，而是「**启动 root 管理器**」；其**子选择** = 「**系统默认 KernelSU（默认）**」或「**检测到的其它受支持管理器**」。**「系统默认 KernelSU」= 不发射任何 `payload` 键**（保持今天的逐字节行为）。
  - **二期拆分（必须写清，避免误判）**：**本批 UI 只影响「跳转目标」（App 侧）**；**wire 发射属下一批 native**——当前 native 只接受 `tier ∈ {exec, script, ko}`，**发 `root` 会被拒**（`payload.root.*` 见上文本节）。⇒ **「UI 已可选 ≠ 已发射」**：UI 可选集合与 wire 接受集合是两件事，后者以 native 落地为准。
  - **检测规则**（与「无论哪种管理器都要检查是否存在且可启动」同源）：**未安装的不列出**（或置灰 + 具名原因），**不得**让用户选一个系统里不存在的目标；检测失败/不可启动 ⇒ 不跳转、只提示。
  - **包名白名单硬规则（只允许有仓库/官方证据的包名）**：已核实起点 = `src/core/terminal/root_script.cpp:40-50` 的四个查找模式（`me.weishu.kernelsu.pr*`、`me.weishu.kernelsu-*`、`com.resukisu.resukisu*`、`com.kowx712.supermanager*`）+ `lkm/lkm_image.cpp` 的 `me.weishu.kernelsu`；**Android 11+ 必须用 `<queries>` 才能检测**（现已在 `app/src/main/AndroidManifest.xml` 声明：`moe.shizuku.privileged.api`、`me.weishu.kernelsu.pr`、`me.weishu.kernelsu`、`com.resukisu.resukisu`、`com.kowx712.supermanager`）；**未核实的包名不得加入白名单**（与「分支包名不得猜」同规）；FolkPatch `me.yuki.folk` 属 **P2**；
- **两条轴禁止重合（用户裁决 2026-10-05）**：**root 管理器轴不接受用户制品、也没有文件导入 UI**——其制品必须来自**系统里已安装的管理器**（`kernelsu` = 系统 `ksud`；`folkpatch` = **从已安装的 FolkPatch 管理器 APK 提取内置模块**：`getApplicationInfo(pkg).sourceDir` → APK 内取预构建模块 → SHA-256 → no-backup 不可变目录 → 路径+哈希交 native 并复核；不可用 ⇒ 置灰 + 具名原因，fail-closed 且不执行）；**凡「用户自备 `.ko`」一律走 `payload.tier = "ko"`**（§3.15 的 payload 轴），不得挂在 root 管理器轴上——详见 `root-manager-selection-design.md` §4.2；
- **成功后自动跳转 root 管理器界面（**已落地** `e500b407` + `4a02d19b`）**：App 侧**唯一包名镜像** = `app/src/main/kotlin/com/ghostlock/app/ui/RootManagerLaunch.kt` 的 `RootManager` 枚举（注释指向 native `lkm_image.cpp:343-348`；未知 ⇒ `null`，**绝不猜**；P1 加行时同步 schema 行），`RootManagerAction{Launch|Hint|Skip}` + 纯函数 `rootManagerAction(...)` 决定「打开它 / 说明它 / 什么都不做」（`force_attack_test` 不产生 root 状态 ⇒ `Skip`）；KernelSU ⇒ 目标包名 `me.weishu.kernelsu`，且**必须与 native 权威同源**——实现里**集中一处映射**并注释指向 `backend/cve_2026_43284/lkm/lkm_image.cpp:345-347`（`default_root_package()`），**不得**在 UI 里出现第二个包名真相；`custom`/未实现分支不跳转（只提示）；待 root 管理器 P1 放行时一并回填状态；
- **P1/P2 边界（v2，用户口径 + E4）**：**P1 可落地** = `kernelsu`（**KernelSU 及分支共享 `ksud` ⇒ 默认按 ksud 走**，`manager` 可选、行为与今天等价）+ `custom`（用户指定程序/argv，不猜包名）+ **存在性/可启动检查（App 预检 + native 复核，权威在 native）**；**P2 计划中（置灰）** = `folkpatch`（**加载 KernelPatch 模块**：`apd insmod` 式手动重定位 + 绕过 CRC/vermagic + `init_module` + **软重启生效**，官方标注不稳定；需独立机制设计 + 独立真机门禁）；详见 `root-manager-selection-design.md` §2.1 E4/§4.1/§9；
- **可用性**：native 导出矩阵（沿用 `stage_availability` 纪律），**Kotlin 不硬编码**；`folkpatch`/未核实分支一律**「计划中」**（注册、解析接受、**选择门禁拒绝**、UI 置灰）；
- **纪律**：包名/入口**未在代码中验证过的一律不猜**（现状 `schema.hpp:68-77`：非 KernelSU → 空包名）；每个 P2 项都要 file:line 依据 + 真机门禁；
- **待用户确认（TODO，v2 后只剩一条）**：**`kernelpatch.ko` 的制品来源**——首选已定：走**既有导入机制**（no-backup 不可变目录 + 本地 SHA-256 + 原子落盘）；待确认：是否只接受「从 FolkPatch 管理器中提取」、是否允许用户完全自备、UI 是否标注「来源不可验证」。（①分支清单、②FolkPatch 机制、④`manager` 形态均已消解，见设计稿 §11。）

## 3.16 HOCON owner 集与根级键（**重构 ①–④ 已落地 2026-10-05**；native/extractor/App **三侧已定稿**）

> **落地状态（2026-10-05）**：① 根级标量通道（`kernel_major`/`kernel_minor`/`safe_mode`）+ 删 `common`/`countermeasure` owner + **vr_guard (b) profile 面删除** = **`b55708a8`**；②③④ `platform.abi.*` → **`backend.cve_2026_43499.abi.*`**（62 处）+ 43284 执行项 → **`backend.cve_2026_43284.execution.*`** + **`wire_only`** 新机制 + manifest **114 行** = **`23958eb0`**。**三侧已定稿**：native ①②③④、extractor（`a6241bc0`）、**App 侧 ③（测试 + golden）= `c443f5f0`（全绿）**。形状定稿本身仍来自用户裁决：HOCON 只声明**可用项**，**运行时选择权在用户/App**。

**owner 集（fail-closed）**

- **允许**：**`backend.<id>`**；根级标量 `schema_version` / `release` / `kernel_major` / `kernel_minor` / `safe_mode`（**在 manifest 里以 `owner = root`、`path = 裸键名` 单列**）；以及根级 `available { <backend> = [ tokens ] }`；
  > **⏸ 已被 2026-10-05 裁决取代（沿革保留）**：`available{ <backend> = [ … ] }` 的**值**将从**预烘焙组合 token 列表**改为**步骤队列**（可读性优先，原则 2「显式优于隐式」）；**两级结构保留**（先 backend，再其下的队列）。**不做**完全动态 DSL / 运行期自适应规划（队列是**静态声明**）。详见 §3.19。
  >
- **已删除（出现即拒）**：**`common` owner**、**`countermeasure.*` owner**（**`b55708a8`**：`common.vr_guard` + `countermeasure.vivo_vr_guard.tracepoint_funcs` 连同 wire/manifest 行删除 ⇒ owner 变空 ⇒ 移出白名单；`platform/vivo/**` 与 `steps.cpp` 两处调用**已于 (a) 期删除**——`src/core/platform/vivo/**` **8 文件** + `platform_vivo_test.cpp` + `host/ancillary_stub.cpp` 删除、`steps.cpp` **−95 行**（**已完成 = `4a182217`**）；**真机门禁 PASS 已归档**（`docs/analysis/device-gates/vrguard-a-20261006/`：`child is root!` → handoff `sent=1` → `KernelSU ready`，route `success=1`，设备未重启），**非行为差异**：`w2b` run-state 标记随祖先块删除 ⇒ 日志 stage 轨迹少一项（`enter/complete("w2b")`），**行为无变化**；**门禁数字**：host `EXIT=0`（告警 **9** 基线、**58 tests**、防火墙 **`174 files, 4/4/0/0`（182 → 174）**）· lint **0** · NDK **0**）、**`platform` owner**（**`23958eb0`**：`platform.abi.*` → **`backend.cve_2026_43499.abi.*`**，62 处）、**`selection { backend, terminal }`**（**`terminal` 概念从 HOCON 移除**，token 已蕴含）；
- **当前注释中（出现即拒）**：**`plugin`** 与 **`payload`**（用户指令 2026-10-05；代码/测试保留，**恢复＝撤销注释 + 跑门禁**）；
- **owner 白名单已收敛为 `backend.<id>`**（2026-10-05 `23958eb0` 落地后）：`platform.` 已删、`common`/`countermeasure` 已删、`plugin`/`payload` 冻结拒收；
- 根级标量是**封闭白名单**：白名单外的一律拒绝（含未知顶层 owner，如 `plugins` / `payloads` / `root`）。

**根级与 43284 的具体搬迁**

- **`kernel_minor` 为重构新增**；`kernel_major` / `kernel_minor` **保留**（「以后有用」）；`safe_mode` 由 `common` 上提为根级标量；
- **43284**：`steps` 留在 backend 顶层（**选择轴**）；`late_load_args` / `selinux_exec_context` / `module_poll_attempts` / `module_poll_interval_ms` / `wait_timeout_ms` → **`execution.*`**；
- **`kmi` / `lkm_path` / `carrier_path` 从 profile 删除**：**wire 字段保留**，由 native **运行时现算注入**；`lkm_path` / `carrier_path` / 各 `.ko` 路径统一在 **GhostLock 内部目录**解析；
- **`available` 两级**：先选可用 backend（键），再在其下选组合 token（值）；运行时的选择写入 wire 的 **`backend.<id>.steps`**；**根级 `available` 不是 wire 段**；
- **消歧**：`index.conf` 的 `backends = [{ id, available }]` 改名 **`usable`**（构建/资产层语义），与 profile 层的 `ghostlock.available{}` **刻意不同名**；
- **extractor**：`tools/extract_rs --format conf` **同批产出新形状**（**已完成**：过渡层 `translate_conf_path` 删除 = **`a6241bc0`**，最终词汇 only）；`profile-legacy/*.conf`（v1 输入夹具）**保留旧形状**。


### 3.16.1 根段承载（`kRootSection`）——**不要改回具名成员**

- **机制**：根级标量用**空段名「根段」**承载（`document.hpp` 的 `kRootSection`），使 owner bind 的**唯一取值路径** `find_value(section, key)` 与根键**同构**；
- **理由（真实缺陷模式）**：bind 路径上有**两处按 section 拷贝的过滤副本**；若根键改用**具名成员**，漏拷一处即**静默 `kernel_major=0`**（不报错、不 fail-closed）；空段名让两处副本天然覆盖根键；
- **纪律**：**勿改回具名成员**（AGENTS 已写明）；新增根级标量时同时更新 manifest 的 `root` 行与本机制；

### 3.16.2 `wire_only` FieldSpec 标记——**manifest = profile 可写面，不是 wire 面**

- **语义**：`FieldSpec` 上的 `wire_only` 表示「**wire/解码接受，但不进 manifest**」⇒ App 的「**profile 可写面**」**不含**它；
- **用于**：`kmi` / `lkm_path` / `carrier_path`——**wire 字段保留**（native 运行时现算注入）、**profile 拒写**（配置里出现即拒）；
- **关键区分**：**manifest = 「profile 可写面」**（App 据此渲染/校验可写键），**不是 wire 面**；wire 面由 native 的解码/绑定路径定义。写文档或写 UI 时不得把两者混为一谈；
- **物证**：`kmi`/`lkm_path`/`carrier_path` 在 manifest **0 命中**（`68bd7a506a210077` 版），而三约定键的 wire 保留正例仍绿（`profile_v3_test.cpp:195-242`）；

### 3.16.3 同类静默错误与单一谓词（`is_cve_2026_43284_section()`）

- **错误模式**：43284 的 `execution` 段落成后，**按 section 拷贝的过滤副本必须同时拷「顶层」与 `execution` 两段**；漏一段 ⇒ **5 个字段（`late_load_args` / `selinux_exec_context` / `module_poll_attempts` / `module_poll_interval_ms` / `wait_timeout_ms`）静默回落默认值**（与 §3.16.1 的 `kernel_major=0` 同类）；
- **消除方式**：native 用**单一谓词 `is_cve_2026_43284_section()`** 判定，**生产路径与测试 shim 各一处**共用同一谓词 ⇒ 两处副本不可能再分叉；
- **纪律**：凡「按 section 过滤/拷贝」的代码，section 集合必须来自**一个谓词/一份列表**，不得在两处各写一遍；

**物证（2026-10-05）**：manifest **114 行**（= 10 头 + **104 字段**）、两份逐字节一致 **sha256 `68bd7a506a210077`**、裸跑 `ok (104 fields, both copies)`；样例行：`root	kernel_major	uint	0	-	profile	-`、`cve_2026_43499	backend.cve_2026_43499.abi.offset.init_task	…`、`cve_2026_43284	backend.cve_2026_43284.execution.wait_timeout_ms	uint	0	literal:15000	profile	…`；门禁：host `EXIT=0`（告警 9 基线、58 tests、防火墙 `180/4/4/0/0`）· lint 0 · NDK 0；**六条负例**在 `src/core/tests/profile_v3_test.cpp:195-242`（`common.*` / `countermeasure.*` / 段内 `kernel_major` / 旧 `platform.abi.*` / 旧扁平 43284 键 ⇒ 拒；三约定键 wire 保留的正例仍绿）。
**与冻结的关系**：插件 / payload 条目按文首冻结清单**不受本节影响**（它们的 owner 仍处于「注释掉、出现即拒」状态）。

## 3.17 构建期跨端清单：`lkm-kmi-manifest.tsv`（**与既有 manifest 同规**）

> **用户指令（2026-10-05）**：**构建逻辑一律进 Gradle KTS，禁用独立 `.sh` 构建脚本**；**跨端列表不得手抄**。LKM 的 **8 个 KMI label** 因此由 **native 导出 manifest**，Gradle 与 Kotlin 只消费。**状态：实现中**（形状定稿；提交号/生成命令与两份副本路径待 native 落地后由 docs-uml 回填）。

- **列**：`label / android_release / kmi / ko_filename`（8 行）；
- **单一真相**：native 导出，**两份逐字节一致**（对拍副本 + 运行时副本）——与 `profile-manifest-v3.tsv`（字段表）、`combination-manifest.tsv`（组合 token）、`vocabulary-manifest.tsv`（组件词汇）**同一制度**：*native 是唯一权威，其它端只读消费、由测试对拍*；
- **消费方**：Gradle（`buildLkmImages` 逐 KMI 构建、`copyLkmIntoAssets` 校验落 assets）与 Kotlin（UI/校验）；**任何一端都不得手写这 8 个 label**；
- **构建入口（Gradle-only）**：`buildLkmImages`（**显式任务**，需容器引擎 `podman`→`docker`，可 `-PcontainerEngine=` 覆盖）与 `copyLkmIntoAssets`（**fail-closed** 校验，挂 `merge*Assets`、**不挂** `preBuild`）；`tools/lkm/ghostlock/Makefile`（**容器内**配方）与 `root_cmd.sh`（**设备端**载荷）**保留**；
- **跨平台硬要求**：构建脚本不得依赖 `shasum`/`sha256sum`/`mkdir -p`/`mv`/`cp`/`find`/bash（用 JVM/Gradle API：`MessageDigest` / `Copy` / `Sync` / `FileTree`）；工具链（如 `llvm-objcopy`）由既有 NDK 解析器（`android.ndkDirectory`）定位，**不依赖 PATH**，prebuilt host tag 用通配；
- **`.sh` 的边界**：仅允许**设备端与运维**（如 `root_cmd.sh` 是经 `call_usermodehelper` 执行的设备载荷、`tools/device-guard/*`、`.github/scripts/*`）；**构建**一律不得用 `.sh`。
- **设计意图的实证（2026-10-06，探针重采）**：`ko_vermagic=5.15.202-android13-5.15.202_r00-dirty` 与设备 `5.15.189-…` **不同字**，但 precheck 仍 **`match=1` / `ver_diff=None`** ⇒ **判定基于 KMI label（`5015`），不是逐字 vermagic** ⇒ 印证「**label 是交付身份、文件名跟随 label**」（`ko_filename` 由 label 派生，不由 vermagic 派生）。**物证**：`docs/analysis/device-gates/probe-resample-20261006-010653/`（原始 stdout 2470 B + `ko.sha256`；提交 `45725ba2`），**取代**旧摘要式基线 `B5-9a-20261003-readonly-probe-pass.md`（沿革保留：旧基线只存 11 行摘要，新基线存原始 stdout 全文，结论无实质差异）。

## 3.18 词汇重命名（stepset）与两轴区分（**已被队列裁决吸收：stepset 不再是用户选择面**）

> **用户原话**：「把**后端中 43499 的 w1w2/w1w3 名称全部换成 shizuku_rootchild/rootchild**」。**实现由 `native-hocon`（native）与 `kotlin-i18n`（Kotlin）并行进行** ⇒ 状态记「**实现中**」（提交号落地后回填）。
>
> **⏸ 已被 2026-10-05「队列取代 token」裁决吸收（沿革保留）**：**stepset 不再是用户选择面**——HOCON 里写的是**步骤 id**（`w1`/`w2`/`w3`…），`w1_w2`/`w1_w3` **只余「预设/归一化名」**（内部用于 `supported` 判定与 dispatch），**改名任务取消**。本节其余论述（尤其「两轴正交」与 43284 一 × 三 path 的硬证据）**仍然有效**，并已并入 §3.19 的连锁影响。

**重命名（只此一条轴）**

| 项 | 旧 | 新 | 说明 |
|---|---|---|---|
| 词汇 token（manifest `stepset` 行） | `w1_w2` | **`shizuku_rootchild`** | 数字 wire id **1 不变** |
| 词汇 token | `w1_w3` | **`rootchild`** | 数字 wire id **2 不变** |
| 第三个 token | `pagecache_write` | 不变 | id 3 |
| C++ | `StepSetKind::W1W2` / `W1W3`（`W1W2Steps`/`W1W3Steps`、`ChainId::Cve43499W1W2/W1W3`、`Cve43499_W1W2/W1W3` 别名） | **`ShizukuRootchild` / `Rootchild`**（随之改名） | **数字 id 1/2 不变**；实现中 |
| 其它轴 | `PathKind` / `FrontendKind` / `TerminalKind` | **保持原样** | 本次**只改 stepset 轴** |

**语义（为什么叫这两个名字）**：`W1W2`（id 1）＝**跳过 seccomp 绕过**，shell 入口/内核派生启动 ⇒ **Shizuku 路径**；`W1W3`（id 2）＝**包含 seccomp 绕过**，app 后代启动 ⇒ **rootchild 路径**。

**两轴的区别（写清，避免再混）**：

> **为什么两轴不能合并（硬证据，2026-10-05）**：`kCombinationCatalog` 全 12 行摊开后，**stepset 与 path 不是 1:1**：
>
> - **43284：同一个 stepset `pagecache_write` 对应三个 path** —— `umh`（`PathKind::Umh` · `umh_forward` · **可用**）、`rootchild`（`PathKind::Rootchild` · `root_child` · 计划）、`shizuku`（`PathKind::Shizuku` · `root_child` · 计划）；⇒ **用 path 名去命名 stepset 在 43284 上直接自相矛盾**（一个 stepset 无法同时叫 `shizuku_rootchild`、`rootchild` …）；
> - **43499：两者只是近似重合** —— `w1_w3` 同时被 `*_rootchild`（`PathKind::Rootchild`）与 `*_umh`（`PathKind::Umh`，计划）使用，`w1_w2` 被 `*_shizuku` 使用；⇒ `Umh` 与 `Rootchild` **共用一个 stepset**，同样不是 1:1。
>
> ⇒ **stepset 必须按「跑哪些 W 阶段」命名（或另立中性名，如 `seccomp_bypass` 一类），不能用 path/terminal 名**；两轴合并会造成不可命名的矛盾。

- **`stepset` 轴 = 「跑哪些 W 阶段」**（`W1`→`W2`（→`W3`）的执行集合；决定是否含 seccomp 绕过）；
- **`frontend` / `terminal` 轴 = 「谁接管」**（`root_child` / `umh_forward` 等终端形态；或前端入口形态）；
- 二者**正交**：同一个 stepset 可以配不同 terminal（例：`rootchild` token 配 `root_child`）；`combination_supported(backend, steps, terminal)` 校验三元组自洽，不自洽直接 `Rejected`；
- **不需要真机门禁**（不进 profile/wire 文档：资产 0 命中）——属**词汇命名**改动。

## 3.19 步骤队列（取代 token）——**决定与沿革**（规则见 §3.20）

> **用户原话**：「**队列直接取代 token 对于 HOCON 的可读性有极大的增强，应该改**；而『完全动态 DSL / 运行期自适应规划』这个你说的对，**不应改**」。
> **状态**：**L 级设计稿已定稿**（`docs/analysis/step-queue-design.md`，**v2.2 = `aec777a9`（定稿，468 行）**；沿革：v1 = `e150eb2b`、v2.1 = `2b6c3f13`）；**定稿规则见 §3.20**，**实现按 M1–M5 分批**（计划条目；设计稿 §4.4）。本节只登记**决定与沿革**。

**决定**

1. **profile/HOCON 不再选「预烘焙组合 token」，改为直接写【步骤队列】**——可读性优先（原则 2「显式优于隐式」）；**两级结构保留**：先选 backend，再写该 backend 下的队列；
2. **不做**完全动态 DSL / 运行期自适应规划（planner / 状态机）：**队列是静态声明**，不是运行期可编程；
3. **被取代的旧决定（沿革保留，不删）**：此前「`available { <backend> = [ tokens ] }` 两级选择」——其**值**由 **token 列表**变为**队列**；**取代理由 = HOCON 可读性**（用户裁决）；旧表述已在 §3.16 标为「已被取代」。

**连锁影响（登记为待设计项；实现细节以设计稿为准）**

| 面 | 影响 |
|---|---|
| `kCombinationCatalog`（12 token） | **降级为内部「归一化键 / 预设判定」**——不再是 HOCON 的用户选择面，但仍用于 **`supported` 判定与 dispatch** |
| `backend.<id>.steps` | 值：**token 字符串 ⇒ 队列** |
| `PathKind` | **按档位 (ii) 删除**（0 读者、非词汇 kind）——与本次裁决同向 |
| `stepset` 改名任务 | **取消**（见 §3.18）：HOCON 里出现的是**步骤 id**（`w1`/`w2`/`w3`），`w1_w2`/`w1_w3` 只余**预设/归一化名** |
| 迁移面 | **62 份资产 + UI + 组合目录 + 契约 + UML**；迁移策略由设计稿给出（**迁移期允许 token 作为语法糖**，资产改写完成后**删除糖**，避免长期双真相） |

**与两轴论述的关系**：§3.18 的「stepset = 跑哪些 W 阶段 / frontend·terminal = 谁接管」**继续成立**；队列描述的就是「跑哪些步骤」，因此**更贴合 stepset 轴的本义**，也回避了「用 path 名命名 stepset」的矛盾（43284 一 × 三 path 的硬证据见 §3.18）。


## 3.20 步骤队列：选择面、route、seam 与支持面（**设计已定稿 2026-10-06**）

> **权威**：`docs/analysis/step-queue-design.md`（**v2.2 = `aec777a9`，定稿**；沿革：v1 = `e150eb2b`、v2.1 = `2b6c3f13`）。**本节规则以 v2.2 为准**（v2.2 并入 U5/U9/U10 三条结论 + S16/S17/S18 守卫）。**本节只登记已定稿规则**；字段语义、诊断文本、改动清单**一律引用设计稿小节，不复制**（一处权威）。**实现分 M1–M5 批次**（见计划条目）；**M1 与本批文档同批提交 = `bcb94253`**（步骤目录 + 编译期注册 + S5 具名诊断 + 两张 manifest 守卫），**M1.1 归一化纯函数 = `2760a599`**（`contract/step_plan.hpp`，24 个 reason token 钉死、生产 0 接线），**M2 进行中（`native-hocon`）**。

**选择面（HOCON ⇄ wire 同形）**

```hocon
available {
  cve_2026_43499 {
    route = "multicast_waiter"                 # 队列级，唯一
    queue = [ { step = "w1" }, { step = "w2" }, { step = "w3" } ]
  }
  cve_2026_43284 {
    queue = [ { step = "pagecache_write" }, { seam = "plugin", stage = "post_terminal" } ]
  }
}
```

- **队列取代 token**；**键名 `queue` 确定**；wire 形态 = **array of map**，**HOCON 与 wire 同形**（`{step:"w1"}` ⇒ `{step:"w1"}`，**不做降级映射**；设计稿 §4.5 / §5-Q1）；
- **元素形态唯一 = 对象数组（B，U9 裁决）**：只有 `{ step = "<id>" }` 与 `{ seam = "<type>", stage = "<stage>" }` 两种；**纯数组（A）不保留**——纯字符串元素/非 map 元素 ⇒ **拒绝** `queue-element-not-object at=<i>`（**可读性交给 UI 预设按钮**，由 App 展开成对象数组再发射；**native 只认对象数组**，不引入第二种 wire 表示）；`params` **预留但未实现（U10 裁决）** ⇒ **拒绝，但报具名 reserved 而非 unknown**：`params-reserved-for-future-step-parameters at=<i>`；契约与 manifest 文档行写明「**reserved, not implemented**」（设计稿 §5.0 / §5-Q1 / §11-U9/U10）；
- **沿革（保留）**：旧「token 两级选择」被取代（**理由：HOCON 可读性**）；「点分索引键」形态**已被用户否决**。

**route：只在队列级（设计稿 §5-Q2，v2.1 = D2′）**

| 情形 | 结果 |
|---|---|
| 43499 缺 `route` | **拒绝** `route-required`（队列取代 token 后 route **没有别的来源**） |
| 43284 出现 `route` | **拒绝** `route-not-applicable`（无 route 轴） |
| route 写进数组元素 | **拒绝** `step-route-not-allowed`（不是忽略、不是取最后一个） |
| 队列级 route 重复声明 | **拒绝** `route-duplicated`（恰好一处） |

`route` 进入 `CanonicalPlan.route`；`supported`/`experimental` 判定、`dispatch_target_of` 与「声明 route == 预设 route」一致性校验**都以它为准**，**不再由 token 隐含**。**明确不做**「每步换 route」（攻击路径改动 ⇒ 独立 L 级设计 + 真机门禁）；**沿革**：v1「每步一律拒绝」→ v2.0「每步可写但整条一致」→ **v2.1「只在队列级」**（证据：`pipeline.hpp:53-62`：`Pipeline` 只实例化一条 route，`Route` 满足整轮 `prepare→execute→disarm→destroy`）。

**canonical 分离与唯一映射点（2026-10-06 落地 = `4c20142f`）**：canonical 里队列级 route 走 **`queue_route`（str，canonical-only 键）**，**几何 Map `route` 一律不覆盖**；**唯一映射点 = `NativeProfile.backendSection()`**（`queue_route` → **wire 键仍是 `route`**；**无几何时直接用 `route`**；**两者同时为 str ⇒ fail-closed「refusing to pick a precedence」**）；`ProfileLayout.validateAvailable` **双形态**（列表零破坏 / 对象 `<backend>{ route=<str>, queue=[{…}], experimental=<bool> }`，五类拒绝）；**回显只认「与 `available` 声明逐值相等」**（手工只在 `backend.<id>` 写 ⇒ 未知键拒绝）。**沿革**：`route` 撞键会在写入时**静默覆盖几何 Map ⇒ 68 资产几何归零**，故必须先分离。**M4 待办（必做）**：`NativeProfileDocument.from()` 缺 accessor ⇒ **声明 `queue` 的 profile 目前不会把 queue 发上 wire**（连同 `app/src/main/.../Profile.kt` 调用点，M4 修）。

**seam：纯占位（设计稿 §5-Q3）**

- **语法**：`{ seam = "plugin", stage = "<stage>" }`（`stage` 必填；`plugin` 是 seam 类型保留名）；
- **阶段词汇 = 单一权威**：**复用冻结插件设计**已定义的 `pre_spawn` / `post_spawn` / `pre_terminal` / `post_terminal`（来源：`plugin.<id>.stage`，契约 §3.14.7 与 `plugin/schema.hpp`），**不新造第二套**；
- **R1 位置约束**：seam 合法位置**只有两类**——队列**之前**（= `pre_spawn`）与终态/驻留窗口**之后**（= `post_terminal`）；落在 `[w1..w3]` 区间内 ⇒ **拒绝** `seam-stage-illegal`（`allowed=[pre_spawn,post_terminal]`）；`stage` 必须与**位置推导的阶段**一致（显式声明 + 交叉校验）；
- **实现仍冻结**：队列侧**只校验，不加载、不映射** `.so`；将来解冻用扩展键 `{ seam = "plugin", plugin = "<id>" }`，**不改位置/阶段语法**。

**支持面判定（设计稿 §4.3 + §11-U5）**

- **归一化**：`queue → CanonicalPlan { backend, route, [step id…], path/terminal }`（步骤**去参数化**；参数不影响 supported/experimental）；
- **与已过真机的预设集合比对**：全等 ⇒ **`supported`**（复用该预设已过真机的 `DispatchTarget`/`Pipeline`，**执行路径与今天逐字节一致**）；合法但不等 ⇒ **`experimental`**；不等且未 opt-in ⇒ **拒绝（fail-closed）**；
- **experimental 需 HOCON 静态声明 `experimental`**：未声明 ⇒ `experimental-not-declared`（**声明是请求，结论由归一化计算**——禁止「自称 supported」，也禁止把已验证预设降级为 experimental）；App 运行前显式标记；
- **日志记录判定**（每个计划一行）：`plan verdict=supported|experimental declared=<0|1> backend=<b> steps=[…] route=<r>`；
- **`kCombinationCatalog` 降级**：12 行 token 表**保留为内部归一化键**（`combination_resolve`/`combination_spec`/`dispatch_target_of` 的输入不再是用户写的字符串）；`available` 列语义改为「已验证预设」；**`PathKind` 按档位 (ii) 删除**（0 读者）。

**步骤词汇与注册（设计稿 §4.1 / §4.2）**

- **`contract/step_catalog.hpp`**（host 可编译、守 R1）+ **步骤词表 manifest**（**native 导出、Kotlin 对拍**，与 `profile-manifest-v3.tsv`/`combination-manifest.tsv`/`vocabulary-manifest.tsv` 同规）；
- **每步字段**：`id` / `display` / `backend` / `slot` / `deps` / `skippable` / `available` / `effects`（语义见设计稿 §4.1 表）；
- **编译期注册**：`kStepCatalog[]` + `static_assert`（id 唯一、同 backend 内 slot 唯一连续、`deps` 只指向同 backend 更小 slot）；执行体声明 `static constexpr StepId step_id`，用 `StepExecution<Exec>` + 折叠 `static_assert` 把**目录表与执行体列表绑死**（**未注册 id 编译不过**）；
- **两层词汇**：`StepSetKind` 与 `vocabulary-manifest.tsv` 的 3 行 `stepset` 保留为**内部归一化 id**（wire 数值不变）；步骤 id 是**更细的一层**；两层映射**同表声明、可对拍**，禁止两处各写一份。

**迁移与门禁口径（设计稿 §4.4 批次表）**：M1 用户面零变化；**M2 = wire 类型面变更（Array + 复合值）⇒ 必须真机门禁**（三条验收判据见设计稿 §4.5/§9）；**M5 删糖是单向门**（token 出现即拒 + 负例 + 真机门禁）。

**M3/M5 已落地（2026-10-06；token 形态已删）**：**M3 = `d34caa99` + `6a3d60c6`**（**62 个资产**从 token 列表迁到对象形态：`route` 由 `CombinationCatalog` 派生、`queue` 由 native **`stepset-steps.tsv`** 展开、**零字面量**；E1 逐资产计划等价全绿；E3 字节清单 **61 changed / 1 identical**；**真机 PASS** 归档 `device-gates/20261006-141317`——其间真机抓到 **`queue-and-token-both-present`** ⇒ 迁移不完整 ⇒ 修掉 token）；**M5 = `e59a8479` + 归档 `af2feefc`**：**HOCON 列表形态出现即拒**（两条独立具名诊断：`the token-list form was removed in M5; declare route+queue` / `empty token list is not a selection; declare route+queue`），App **停发 wire `steps`**，native **具名拒 token**（`plan_error reason=token-form-removed path=backend.<id>.steps hint=declare-route-and-queue`）且**归一化不再物化 token**；**golden 3920** / **`NO_SELECTION_HEX` 3766**（来源与 −44/−92 已写进注释）；两份 manifest **`62ea112b2caf`** 逐字节一致（说明列写明 M5 拒绝语义）；**M5 真机 PASS**（二进制 `24e9accf84b9`）。**§11-U7 决定**：**保留 `steps` 键 + 具名拒取值**（整键移除会让拒绝退化成泛化 unknown-key，DX 更差）。

**载体 / canonical 双路径读取（M5 修正）**：`NativeProfileDocument.from()` 改为「**运行时载体优先、回退 `available.<id>` 声明**」——此前只读声明路径 ⇒ **导出的 `.bin` 静默丢队列**（✗）；**两条路径必须逐值同构**（新增第二条读取路径 ⇒ 同批补跨路径等价对拍）。**legacy uint 口径边界（`native-r1` 实证）**：owner schema 把 `steps` 声明为 `WireKind::String` ⇒ legacy uint 路径**必须保留「就地改写为 token 文本」**（改写**调用方已发来的键**，**不是注入糖**）；不变量 = 「**queue 路径不得创建 `steps` 键**」。

**M4 收口**：M4 的 `queue` 不上 wire 缺口已由 M5 修掉；**仍未做**：**M4(b) 设备端 app 侧装载实跑**（`LkmImageProvisioner.provision` 仅纯逻辑单测 4/4）。**未做（M5 侧）**：两条证伪与三桶计数的原始记录待补。

**守卫与证伪（设计稿 §10.1，v2.2 口径）**：静默默认点（S1/S3/S5/S6/S8/S9/S13）+ 新形状守卫（S14a–S14d / S15 / **S16–S18**）+ §4.2 非法形状 11 行 = **26 条守卫**，全部**硬失败 + 具名诊断**；每条在实现批次须给「**造错点 → 失败输出 → 撤回核验**」三件套，**证伪一律 `make -B`**（防同秒 mtime 跑旧二进制造成假证明）。

## 3.21 兼容性政策：v1 / v2 / v3 矩阵（**用户澄清 2026-10-06**）

> **用户原话**：「**v3 profile 还没正式发布，你想怎么改就怎么改无需兼容上一版；真正要兼容的是 v1 和 v2**」。

| 版本 | 状态 | 兼容策略 |
|---|---|---|
| **v3**（GLKv3 / HOCON `schema_version = 3`） | **未正式发布** | **形状变更无需兼容上一版**——旧 `selection{}` / `common{}` / `platform{}` **一律拒绝（出现即拒）**，**不加迁移、不留过渡读路径**；这正是 HOCON 重构（§3.16）与队列改造（§3.19/§3.20）可以自由改形状的依据 |
| **v1**（旧 HOCON `schema_version = 1`；旧 `offsets.json`） | 需兼容 | **经 Kotlin `LegacyProfileConverter`（唯一迁移点）转换**为 3 并记诊断；native **不解析 v1**（`src/core/legacy/` JSON 路径已删除）；其余版本值一律拒绝（错误带实际值） |
| **v2**（旧 wire bin） | **当前拒绝** | 依 `docs/analysis/s4-r2c-v3-only.md` 的决策「**旧 bin 弃用**」：native 只认 `schema == 3`，v2 读路径与 writer 一并删除（`binary.{hpp,cpp}` 已删）。**⚠ 待用户确认实际需求**——Lead 正在确认是否确实存在需要读取的 v2 文件；**若需要**，另开 **Kotlin-only** 批次恢复导入/迁移（App 侧把旧 bin 转成 v3 文档），**native 仍保持 v3-only** |

**边界**：兼容性只在**读入侧**（App/Kotlin）承担；**native 永远 v3-only**（这也是「Kotlin 与 native 版本绑定、同一分支内直接替换」的推论）。

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
> **⏳ 词汇重命名（2026-10-05，实现中）**：`Cve43499W1W2` → **`ShizukuRootchild`**、`Cve43499W1W3` → **`Rootchild`**（**数字 1/2 不变**）；详见 §3.18。

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
