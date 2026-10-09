# ADR-0014 附：冲突与两条路（虚基类 vs 既有机器判据）

> 本件是 ADR-0014 的组成部分（正文 `0014-kernel-memory-only-surface.md`）· 2026-10-09 · 由 Lead 当场质疑并给出合规替代，待用户裁决

## 6 冲突与两条路（Lead 当场质疑 · 2026-10-09）

**用户要的形态里有一条与本树既有的三条机器判据正面冲突** ⇒ 按守则的「当责」条，先质疑、给代价、给合规替代，由用户裁决。

**冲突点：虚基类 / vtable** ✗（用户原话「提供…**虚基类**，各 backend 再继承」）。**实测证据（我逐条核过）**：

| # | 位置 | 内容 |
|---|---|---|
| 1 | `src/Makefile:806-808` | `nm -C <四个二进制> \| grep -c _ZTV` ⇒ **非零即 `VTT-HIT` + `exit 1`** ⇒ 落地虚基类**当场红**；**实测今天为 0** ✓ |
| 2 | `race/pi_window.hpp:14-16/:40` | 窗口禁令之一：**no indirect dispatch（no vtable, no function-pointer hop, no type-erased callable）** ✓，且注明 **each is machine-checked** ✓；违规项原文「a vtable … makes the window shape unverifiable」✓ |
| 3 | `script/call_sink.hpp:9-11` | 本树**既定选择**已写明：「**Shape: a function-pointer table, not a virtual class** … the gate scans built binaries for `_ZTV` symbols, so no vtable is introduced」✓ |

⇒ 理由不是洁癖 ✓：PI 窗口的**形状必须可验证** ✓（虚调用让「窗口里到底跑了什么」不可判定 ✓）。

### 路径 A（**推荐**，合规且更贴合用户原意）

**用旧树本来就有的 `KernelMemoryOps` —— 一张函数指针表** ✓：

```cpp
struct KernelMemoryOps final {
    std::int32_t (*read) (void *ctx, std::uint64_t address, void *out, std::size_t size) noexcept = nullptr;
    std::int32_t (*write)(void *ctx, std::uint64_t address, const void *in, std::size_t size) noexcept = nullptr;
    [[nodiscard]] bool available() const noexcept { return read != nullptr && write != nullptr; }
};
```

⇒ 三条意图**全部满足** ✓：① **一个接口覆盖所有 backend 能力** ✓（就是这张表 ✓）② **各 backend 实现自己支持的部分** ✓（各自**填表**，未支持者留空 + `available()` ✓ —— 与「继承但只实现支持的」等价 ✓）③ **对外只暴露这一个对象** ✓（Lua 只拿到这张表 ✓，而 `call_sink` 已经是「函数指针表给脚本层」的既有先例 ✓）。
**代价：0** ✓ —— 不碰任何机器判据 ✓；且它**正是旧树 `kernel_memory.hpp` 里的 `KernelMemoryOps`** ✓ ⇒ 比 vtable **更符合**用户说的「像以前的 kernel-memory 一样」✓。
**若要跨 backend 一个类型名** ✓：`variant`/tag + **直接调用的 switch** ✓（禁 `std::visit` 型间接跳转 ✓，`std::function` 亦禁 ✓）。

### 路径 B（**用户强制确认才可走**）

坚持真 vtable ✗ ⇒ 属**越过已批准的结构约束** ✓ ⇒ 须：**强制确认 + 沿革记录 + 技术债登记** ✓，且**同批改**三处：① `pi_window` 的禁令文本与 :40 的违规项 ✓；② `Makefile` 的 `_ZTV` 扫描 ✓（删或放宽 ✓）；③ `call_sink` 的既定选择注释 ✓ —— 即**明确承认「窗口形状不可验证」**这一代价 ✓。

⇒ **Lead 建议走 A** ✓：它零代价、不破判据，且与用户原话的「像以前一样」**更一致** ✓。

## 用户裁决（2026-10-09）：**走路径 B —— 要真 vtable**

**用户原话**：「要真 vtable，虚分派只是调用 PI，不在 PI 期间内发动 syscall 就行，并且重新设计 kernel_memory，改个符合作用的名字，这将会成为唯一暴露给 lua 的东西」

⇒ 这是**明确、知情的强制确认**（守则「唯一例外」条）：越过已批准的结构约束 ⇒ **执行**，但同批留**沿革 + 技术债**。

### 1 沿革（越过哪一条、谁确认、代价为何）

| 项 | 内容 |
|---|---|
| 越过的约束 | `race/pi_window.hpp` 的 **no indirect dispatch**（机器检查）· `Makefile:806-808` 的 `_ZTV` 扫描 · `script/call_sink.hpp` 的「function-pointer table, not a virtual class」既定选择 |
| 谁确认 | **用户**（2026-10-09，原话见上） |
| 用户给的论证 | **虚分派本身只是进入 PI**；真正的禁令是「**PI 期间不得发动 syscall**」⇒ 只要**不在 PI 内发 syscall**，窗口之外的虚调用是安全的 |
| 代价 | **`_ZTV` 扫描这个「窗口无间接派发」的代理判据失效**（虚表进二进制即必红）⇒ 必须**换成真正表达该性质的判据**，否则等于丢掉一条机器检查的不变式 |

### 2 技术债（登记）

| # | 债务 | 偿还时机 |
|---|---|---|
| **D1** | `pi_window` 禁令文本改写：从 no indirect dispatch 改成**用户论证所依据的那条** —— 窗口体内**不得发动 syscall、不得间接调用** | 与本批同落地 |
| **D2** | `Makefile:806-808` 的 `_ZTV` 扫描**替换**为**窗口体级检查**（对窗口函数做符号/反汇编级检查：无间接调用、无 syscall 调用）⇒ 不是删，是**把代理判据换成直接判据** | 与虚表落地**同批**（否则 check 当场红） |
| **D3** | `call_sink` 的既定选择注释改写并指向本裁决 | 与本批同落地 |

### 3 重新设计的三条要求（用户原话）

1. **重新设计 kernel_memory**（旧件是 `KernelMemoryOps` 函数指针表与 `KernelMemory` 虚基类混用 ⇒ 新设计**只保留虚基类这条路**，并做成**唯一暴露面**）；
2. **改个符合作用的名字** ⇒ Lead 提案（可改）：**`KernelAccess`** —— 它的作用不是「内存」而是「**按地址读写内核**」（read/write/write_zero + 通道/载体 ⇒ 访问原语）；备选 `KernelPrimitives` / `KernelDirectAccess`；
3. **它是唯一暴露给 Lua 的东西** ⇒ 组合根**只**把它交给 Lua 侧；不再暴露 `backend`/`writeop`/`handoff` 之类的上层。

### 4 由此作废的旧限制

- 早前设计的「`window.*` 不是脚本可见项」与其三形态提案 ⇒ **随本裁决作废**（上层步骤上移 Lua ⇒ Lua 只看 `KernelAccess`）；
- `CapabilityKind` 位集仍用于**声明该实现支持哪些能力**（与本裁决不冲突）。

### 5 用户同日的五条细化（已落进 contract/kernel_access.hpp）

| # | 要求 | 落地 |
|---|---|---|
| 1 | **能力状态，不只是布尔** | `AccessState`：Unsupported / Declared / Initializing / Ready / Failed ⇒ 例：43499 先能写，要等窗口开了才能任意读 |
| 2 | **虚基类里放 factory** | `static std::unique_ptr<KernelAccess> create(std::string_view backend)` —— 本层声明、在认识 backends 的地方定义（本层不得去够 backend 层） |
| 3 | **后端自管生命周期** | `initialize(...)` / `shutdown()` ⇒ 调用方只请求，对象持有所打开的一切 |
| 4 | **establish 改名 initialize** | 已改名 ⇒ 它是能力的状态变化，不是建立连接 |
| 5 | **能力有附加参数（AccessParameters 虚基类）** | 后端自定义参数集：43499 的写需要 route + route 偏移 ⇒ 操作收参数基类，名字不匹配 ⇒ BadParameters（不猜不兜底） |

另：NotInitialized 与 BadParameters 都是具名拒绝 —— 未初始化就用、或用错参数集，都按名拒绝而不是失败在下游。

### 6 用户同日的第二批细化（七条，已落进 contract/kernel_access.hpp）

| # | 要求 | 落地 |
|---|---|---|
| 1 | `AccessParameters` 改名 **`AccessMiscParameters`** —— 地址与写入值都在操作签名里，参数对象只装**其余** | 已改名（嵌套在 `KernelAccess` 内） |
| 2 | **read64/write64 为什么不是虚的** —— 43499 初期只能整字写 | 改为 **virtual**（带默认实现、可覆写）⇒ 只会整字写的实现，只写这两个就是它的全部读写面 |
| 3 | **span 有没有长度** | **有**（span::size()）⇒ 写接口回 `AccessResult<size_t>` = 写了几个字节 |
| 4 | **别的 backend 没有可控位宽怎么办** | 新增 **supports_width(bytes)** + **BadWidth** ⇒ 位宽是**能力**不是假设 |
| 5 | **Status 与 AccessMiscParameters 定义放进 KernelAccess 内** | 全部**嵌套**（KernelAccess::Status / ::AccessResult<T> / ::AccessState / ::AccessMiscParameters / ::NoMiscParameters）⇒ 对外只有一个名字 |
| 6 | **AccessStatus 改名 AccessResult 且泛型** | `template <class Value> struct AccessResult { Status status; Value value; ok(); }` ⇒ 状态 + 返回值 |
| 7 | **read 不传指针，直接在 AccessResult 里回新 span** | `read(...) -> AccessResult<span<const byte>>`（span 指向实现自有存储，下一次调用前有效）；`write(...) -> AccessResult<size_t>` |

**同日修过的两处自身缺陷**（如实记）：① 测试在**未检查 ok()** 时索引 value ⇒ 越界崩溃（139）⇒ 已加护栏；② 假件里参数名曾叫 bytes，**遮蔽成员数组** ⇒ sizeof(bytes) 取到 8 而非 64 ⇒ 边界判据把自己拒了 ⇒ 已改名。
