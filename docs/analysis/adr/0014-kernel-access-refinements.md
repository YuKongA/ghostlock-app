# ADR-0014 附：KernelAccess 的两批细化（逐条落地）

> 本件是 ADR-0014 的组成部分（正文 `0014-kernel-memory-only-surface.md` · 冲突件 `0014-kernel-memory-conflict.md`）· 2026-10-09

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
