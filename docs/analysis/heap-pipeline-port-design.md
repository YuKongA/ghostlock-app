# 堆/页管线移植设计（43499 的页准备）

> 分析类（≤8 KiB）· **L 级**（攻击关键路径）· 作者 Lead · 依据 = 旧树 `spray.cpp` 的 `prepare_kernel_page`（**实测步骤序列** ✓）
> 目标：让**重写后的** 43499 在**真机**上能做完页准备 ⇒ 这是「两 backend 真机可用」的第 1 优先级 ✓

## 1 旧实现的实测步骤（按调用顺序，行号为证）

| # | 步骤 | 性质 |
|---|---|---|
| 1 | `close_reclaim_sockets()` + `cleanup_page_prepare_state()` | **决定**（该关谁 ✓ 已由 `reclaim_plan` 覆盖一部分 ✓） |
| 2 | `prepare_ctxs()` | 决定（几何：每组几个 ✓ 来自 profile ✓） |
| 3 | pre 组：`clone_child()` + `open_memfd()` ×N | **系统调用**（fork/memfd ✓） |
| 4 | spray 组：同上 ×M | 系统调用 |
| 5 | `KernelSnitchOwner::create(...)`（用 `MM_ORDER`、`cpu_count`、**profile 的碰撞数** ✓） | 机制 + 决定 |
| 6 | pre/post/leak 组：`clone_child()` + `open_memfd()` | 系统调用 |
| 7 | 杀 pre/post/spray 的**部分**子进程（保留 post 末位 ✓ —— 与 `reclaim_plan` 的规则一致 ✓） | **决定** |
| 8 | 泄漏循环：等 snitch 命中（`heap_kernelsnitch_timeout_ms` ✓ profile） | 决定 + 等待 |
| 9 | 形状化：`socketpair` ×2 ✓ `setsockopt(SO_SNDBUF)` ✓ `fcntl(O_NONBLOCK)` ✓ `sendmsg`（`SKB_SEND_SIZE` ✓） | 系统调用 + 几何 |
| 10 | 绑核（`main_cpu` ✓）+ 4×`sched_yield` | 决定（时序 ✓ profile） |
| 11 | **回收序列**（关 pre 全 ✓ / post 除末位 ✓ / spray 每 slab ✓ / shaping 对 ✓ / **leak 最后** ✓） | **决定**（**已入库** ✓ `reclaim_plan` ✓） |
| 12 | 返回 base（`leaked & ~(ORDER3_SIZE-1)` ✓） | 决定（对齐 ✓ 几何 ✓） |

## 2 移植形态（与树内既有模式一致 ✓）

1. **决策面先落地**（可测 ✓ 无内核 ✓）：`prepare_plan`（各组数量 ✓ 由 profile 几何驱动 ✓）· `reclaim_plan`（**已入库** ✓）· `align_base`（对齐 ✓ 几何 ✓）· `shaping_plan`（要开的 socket 对与参数 ✓）。
2. **系统调用面走 seam** ✓（同 `futex_seam_linux.hpp` 的先例 ✓）：`HeapSeam` 声明 `clone_child` / `open_memfd` / `socketpair` / `setsockopt` / `fcntl` / `sendmsg` / `close` / `kill_child` / `sched_yield` / `pin_to_core` ✓ ⇒ **主机用假 seam 全覆盖** ✓、**真机用真 seam** ✓。
3. **编排**（`prepare_page`）：按 §1 的**顺序**调用决策面与 seam ✓ —— **顺序本身是机制** ✓（与 `reclaim_plan` 同理 ✓）。
4. **状态**：本轮**不新增可变全局** ✗（AGENTS 只允许 `g_exploit_session` ✓）⇒ 编排的持有物放调用方栈上 ✓ 或经参数传递 ✓。

## 3 真机判据（分步可独立验，避免一大步才知道 ✓）

| 步 | 真机判据（可执行 ✓） |
|---|---|
| 决策面 | 主机测试 ✓（数量/对齐/回收顺序/形状参数与旧值**逐值相同** ✓） |
| seam 真机 | **单独探针**（如 `open_memfd` 真开真关 ✓、`socketpair+sendmsg` 真发 ✓）⇒ 每条打印 errno ✓ |
| 编排真机 | 跑一次 `prepare_page` ⇒ 打印**每组实际数量**、**snitch 是否命中**、**回收是否按序** ✓；失败给具名原因 ✓ |
| 与旧树对拍 | **同机同档**跑旧二进制与重写版 ⇒ 比较**同一批可观测项**（数量/命中/耗时量级 ✓）—— 不必逐字节相同 ✓ |

## 4 反面情形（明说不做）

- ✗ **不抄旧树的全局状态**（`g_exploit_session` 之外一律新增即违规 ✓）；
- ✗ **不引入新 CLI 旁路**来试（生产路径 + 参数化 ✓，AGENTS 明文 ✓）；
- ✗ **不猜几何**：组数/耗时/超时一律来自 profile ✓（新键须走裁定 + manifest ✓）；
- ✗ **不一次性移植完**：先决策面 ✓ 再 seam ✓ 再编排 ✓ —— 每步真机可验 ✓。

## 5 待评审问题

1. §2.2 的 seam 边界是否合适（snitch 是否应并入 seam ✓，还是作为独立机制 ✓）？
2. §3 的「与旧树对拍」能否作为门禁判据 ✓（还是必须逐值相同 ✗）？
3. 组数/几何来自 profile ⇒ 若**键不存在**（旧树的 prepare 组数 ✓ 可能没上 wire ✗）⇒ 是补键（走裁定 ✓）还是判为机制常量 ✓？
