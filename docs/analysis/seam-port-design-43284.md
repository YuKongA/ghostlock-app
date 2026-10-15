# 附：第二个 backend（43284）的实测

> 本件是 `landing-port-design.md` 的附件（正文只留 43499 的缝与切片）· 2026-10-09

## 7 第二个 backend（43284）的实测规模与它要用的通道

新树里 43284 基本是空的：只有 `lkm/module_facts.{hpp,cpp}`。旧树是一整个 backend：

- `cve_2026_43284/` 共 47 件，C++ 行数合计 10781 行
- 与访问路径相关的大件：`execution_binding.cpp`（350）、`diagnostic.cpp`（297）、`entry.cpp`（78）；`backend_terminal.cpp`（580）属终端，不是访问
- 通道面：`lkm/`（模块镜像与策略）、`ipsec/`（含 `aes256`）、以及页缓存相关件

它映射到访问接口的方式（与 43499 不同轴，但接口已能表达）：

- 43499 = 竞争窗口写，即 `AccessChannel::PiWindow` 加 `AccessCarrier::KernelPage`
- 43284 = 已加载模块或页缓存写，即 `AccessChannel::KernelModule` 或 `PageCache`，加 `AccessCarrier::ModuleWindow` 或 `FilePage`
- 这正是把通道与载体做成枚举而不是布尔的好处：两个 backend 用同一张接口，只有选择不同

因此 43284 的访问实现不是从零写，而是有选择地搬：搬它的写路径（模块面加它落地写的那一段），不搬终端与诊断（那是别的轴）。

还没量：这 10781 行里属于写路径的到底是多少（未按文件逐件分），以及它的 `initialize` 要准备什么（模块装载？镜像校验？）。动它的下一个切片前必须先量这两件。

## 8 43284 写路径与其它轴的切分（实测，按行数）

把旧侧 47 件按行数排序后，边界比预期清楚：大头并不是写。

**属于访问（写路径）的**：

- `pagecache/pagecache.cpp` 305（页缓存写）
- `real_ops.cpp` 606（真实操作）
- `lkm/lkm_image.cpp` 622 加 `lkm/lkm_policy.hpp` 234（模块镜像与策略：写要靠它）
- `execution_binding.cpp` 350（绑定）
- 合计约 2100 行，约占该 backend 的 20%

**不属于访问（别的轴，不要搬）**：

- `stage_runner.cpp` 793 加 `.hpp` 298（阶段机）
- `steps/elf_hook.cpp` 726、`steps/chain.cpp` 670 加 `.hpp` 399、`steps/hook_patch.cpp` 325、`steps/shellcode.cpp` 283（钩子与链：终端）
- `backend_terminal.cpp` 580、`diagnostic.cpp` 297（终端与诊断）
- 合计约 3800 行以上

⇒ 结论：**43284 的访问实现 = 那 2100 行里与落地写相关的那部分**，终端与钩子链不在其中。下一步动它之前，仍要先读 `real_ops.cpp` 与 `pagecache.cpp` 的公开面，确认哪些函数真的属于写、哪些是被终端调用的。

## 9 43284 的写面比 2100 行更小（实测公开面）

读了两个候选件的公开面后，写路径的范围又收窄了一次：

**`pagecache/pagecache.hpp`（这才是访问面）**：

- `enum class WriteResult : std::int32_t`
- `struct PageCacheWriteStats`
- `struct PageCacheWriteContext` —— 一个上下文结构，正是窗口的形状
- `void zeroize(PageCacheWriteContext &)` —— 与 43499 的 zero 引导同位

**`real_ops.hpp`**：`ReadOnlyChainContext`、`real_read_block(void *ctx, uint64_t offset, ...)`（读）、`RealChainContext`（含 `plugin_dispatch` 指针，而插件面按用户指令是暂停的）。

⇒ 于是 43284 的访问实现对应关系是：**`PageCacheWriteContext` 就是它的窗口**，`AccessChannel::PageCache` 加 `AccessCarrier::FilePage` 就是它的通道与载体，`zeroize` 对应零写。**读**那一侧另有 `real_read_block`，与 43499 的读同属一条规则（先声明、备好事实、否则按名拒绝）。

**因此它的第一片比 §8 的估算更小**：真正要搬的是 `pagecache.cpp`（305 行）与它的上下文，而不是那 2100 行整体；`lkm` 与 `execution_binding` 属于终端侧的装载，按 ADR-0006 的归属**不搬进访问**。

**还没量**：`PageCacheWriteContext` 的字段（未逐个读），以及 `pagecache.cpp` 里对旧会话/全局的耦合数（与 43499 的 22 处同类）。这两件是它切片 1 的输入。

## 10 43284 的上下文、写宽与耦合数（实测）

### 它的窗口就是它的会话状态

`PageCacheWriteContext` 的字段实测：`IpsecSaParams sa`、`file_fd`、`socket_fd`、`next_seq`、`pipe_flags`、`SpliceIoOps io`、`OldPageSource old_page`、`HelperWriteSource helper_write`、`PageCacheWriteStats stats`。

⇒ 这正是一个 route 的会话级状态（**起一条 route 要备的东西**），与 43499 的 `TargetState` 同层、内容不同。它映到缝的 `bring_up`（备）与 `tear_down`（拆，另有现成的 `zeroize`）。

### 写宽是 16 字节，不是 8

它的公开面是 `write16(ctx, file_offset, const uint8_t bytes16[16]) -> WriteResult` 与 `write_block(ctx, file_offset, bytes, len)`（逐 16 字节块推进，`len` 必须是 16 的倍数，`len == 0` 是空操作 Ok）。

⇒ **这是实打实的一处差异**：43499 的窗口写**一个字（8 字节）**，43284 的页缓存写**一块 16 字节**。两条路都真实存在，而接口早就为此留了位：`supports_width(bytes)` 让 43499 只认 8、43284 只认 16，`BadWidth` 是它们的具名拒绝。这也回答了当时那个问题——别的 backend 的写宽不同，不是靠约定，是靠能力回答。

### 耦合数：0

`pagecache/pagecache.cpp`（305 行）对旧会话与两个全局的耦合数是 **0**；而 43499 的 `multicast_waiter_route.cpp` 是 **22**。

⇒ 也就是说 **43284 的访问比 43499 更干净地可搬**：它的会话状态已经是一个显式结构（`PageCacheWriteContext`），不需要像 43499 那样把 22 处隐式取值改成显式传参。它的 `bring_up` 就是把这个结构的字段备齐（键与套接字、序列号、io 面、可选来源），`land_word` 就是 `write16`。

**还没量**：`write16` 内部对 `sa` 与 `next_seq` 的推进语义（未逐行读），以及 `IpsecSaParams` 从哪来（是否来自 profile，若是则按 ADR-0015 改由脚本传入）。
