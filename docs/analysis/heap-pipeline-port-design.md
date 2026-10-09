# 堆/页管线移植设计（43499 的页准备）· v2

> 分析类（≤8 KiB）· **L 级**（攻击关键路径）· 作者 Lead · v1 评审见 design-critic（0 阻断 / 重要 4）
> 目标：让**重写后的** 43499 在**真机**上能做完页准备 ⇒ 「两 backend 真机可用」的第 1 优先级 ✓

## 0 v2 改了什么（逐条对 H1–H5 与三问判定）

| 项 | 处置 |
|---|---|
| **H1** | **顺序权威 = `src/core/backend/cve_2026_43499/memory/prepare_steps.hpp`**（本轮已入库 ✓ 12 步数据 + 三条关系断言 ✓）⇒ **`prepare_plan` 只补「每组数量与参数」** ✓，**不复制顺序** ✗ |
| **H2** | 引用改为**可复现形式**：`git show b0e9f499^:src/core/backend/cve_2026_43499/spray.cpp`（该文件已被 b0e9f499 删除 ✓）+ **逐步行号**（评审实测 ✓ 见 §1） |
| **H3** | 补漏三步：**skb 缓冲分配与清零** ✓ · **`prepare_skb_payload`** ✓ · **两处早退失败路径**（各自 snitch.reset + 杀 prepare 子进程 + cleanup + **return 0** ✓）⇒ §1 改为**主路径 + 失败路径**两段 ✓，且失败路径**必须进编排** ✓（否则真机留下活子进程与脏状态 ✓） |
| **H4** | 几何量**分两类**（§2.5 ✓）：**设备相关 ⇒ profile 键**（落 `execution.heap.*` —— `heap` scope **资产里已存在** ✓；缺键 ⇒ **具名拒绝、不得 C++ 兜底** ✓）；**内核几何 ⇒ 机制常量**（单一定义处 + 注释 ✓ 同 64 KiB 先例 ✓） |
| **H5** | seam 落点照 `futex_seam_linux.hpp` 同构 ✓：shape 宿主可测 ✓ + `heap_seam_linux.hpp` 设备专用薄边 ✓、**宿主不编译** ✓、**seam 不含任何决策** ✗ |
| **ⓐ** | **snitch 不并入 seam** ✓ —— 它有几何/生命周期/等待 ⇒ 是独立机制 ✓（并入会让假 seam 承担策略 ✓） |
| **ⓑ** | 判据**分层**：**(i) 主机决策面逐值相同** ✓（黄金值固化为测试 ✓）；**(ii) 真机只认具名不变式** ✓；**(iii) 同机对拍 = 旁证** ✓（真机噪声已在 AGENTS 记录 ✓） |
| **ⓒ** | 见 H4 的两类划分 ✓ |

## 1 旧实现（可核引用：`git show b0e9f499^:…/spray.cpp`，行号为评审实测 ✓）

**主路径**

| 行 | 步骤 | 性质 |
|---|---|---|
| 449-450 | `close_reclaim_sockets()` + `cleanup_page_prepare_state()` | 决定 |
| 453 | `prepare_ctxs()` | 决定（几何） |
| 455-456 | **skb 缓冲分配 + 清零** | 决定（尺寸来自几何） |
| 458-461 / 463-466 | pre / spray 组：`clone_child()` + `open_memfd()` | 系统调用 |
| 468-470 | `KernelSnitchOwner::create(...)`（MM_ORDER / cpu_count / 碰撞数） | **独立机制** |
| 636+ | post / leak 组 + 泄漏循环（等命中，按超时 ✓） | 系统调用 + 等待 |
| 600 | `base = leaked & ~(ORDER3_SIZE - 1)` | 决定（对齐） |
| 601 | **`prepare_skb_payload(base, request)`** | 决定 + 写 |
| 610-629 | 形状化：`socketpair` ×2 · `setsockopt(SO_SNDBUF)` · `fcntl(O_NONBLOCK)` · `sendmsg(SKB_SEND_SIZE)` | 系统调用 + 几何 |
| 631-635 | 绑核（`main_cpu`）+ 4×`sched_yield` | 决定（时序） |
| 636+ | **回收序列**（pre 全 · post 除末位 · spray 每 slab · shaping 对 · **leak 最后**） | **决定**（**已入库** ✓ `reclaim_plan` ✓） |

**失败路径（v1 漏，必须进编排 ✓）**

| 行 | 触发 | 动作（旧树原文语义 ✓） |
|---|---|---|
| 588-598 | `leaked` 无效 | `snitch.reset()` + 杀 prepare 子进程 + `cleanup_page_prepare_state()` + **return 0** |
| 601-608 | `prepare_skb_payload` 失败 | 同上（reset + 杀子 + cleanup + **return 0**） |

## 2 移植形态

### 2.1 决策面（主机可测 ✓，**不含内核** ✓）

| 件 | 内容 | 状态 |
|---|---|---|
| `prepare_steps` | **顺序权威**（12 步 + 三条关系断言 ✓） | **已入库** ✓ |
| `reclaim_plan` | 关哪些 fd、什么顺序 ✓ | **已入库** ✓ |
| `prepare_plan` | **只补每组数量与参数**（读 profile ✓，**复用** prepare_steps 的顺序 ✓） | 拟建 |
| `align_base` | 对齐（几何来自 profile ✓） | 拟建 |
| `shaping_plan` | 要开的 socket 对与参数（几何 ✓） | 拟建 |

### 2.2 系统调用面（seam ✓）

`heap_seam_linux.hpp`：`clone_child` / `open_memfd` / `socketpair` / `setsockopt` / `fcntl` / `sendmsg` / `close` / `kill_child` / `sched_yield` / `pin_to_core` ✓ —— **薄边，不含决策** ✗；主机用假 seam 全覆盖 ✓。

### 2.3 编排

`prepare_page`：按 `prepare_steps` 的顺序调用决策面与 seam ✓ —— **顺序本身是机制** ✓；**失败路径显式建模**（§1 第二张表 ✓）。

### 2.4 状态

**不新增可变全局** ✗（只允许 `g_exploit_session` ✓）⇒ 持有物放调用方栈上 ✓ 或参数传递 ✓。

### 2.5 几何量分类（H4 ✓）

| 类 | 例 | 落点 | 缺值行为 |
|---|---|---|---|
| **设备相关** | 每组数量（prepare/spray/post/leak）· 碰撞数 · 各超时 | **profile 键**：`execution.heap.*`（`heap` scope 资产里已有 ✓） | **具名拒绝** ✓（不兜底 ✗） |
| **内核几何** | order-3 页尺寸 · SKB 发送尺寸 · mm_struct 尺寸/stride 机制侧缺省 | **机制常量**（单一定义处 + 注释 ✓，同 64 KiB 先例 ✓） | 不适用（常量 ✓） |

## 3 真机判据（分步 ✓）

| 步 | 判据 |
|---|---|
| 决策面 | **主机**：数量/对齐/回收顺序/形状参数与旧值**逐值相同** ✓（黄金值固化 ✓） |
| seam | **真机单探针**：`open_memfd` 真开真关 ✓ · `socketpair+sendmsg` 真发 ✓ ⇒ 每条打印 errno ✓ |
| 编排 | **真机**：跑一次 ⇒ 打印每组实际数量（== profile 值 ✓）· snitch 是否命中 ✓ · 回收是否按序 ✓ · `base & (ORDER3_SIZE-1) == 0` ✓；失败给**具名**原因 ✓ |
| 对拍 | **旁证**（非门禁 ✓）：同机同档跑旧二进制，比数量/命中/耗时量级 ✓ |

## 4 反面情形（明说不做）

- ✗ 不抄旧树全局状态；✗ 不加实验用 CLI 旁路；✗ 不猜几何（缺键即拒 ✓）；✗ 不一次性移植完（决策面 → seam → 编排，逐步真机可验 ✓）；✗ seam 内不做决策；✗ 不复制步骤顺序（权威唯一 ✓）。

## 5 待评审问题（v2 仅剩一个）

1. §2.5 的两类划分里，**`mm_struct` 尺寸/stride 的「机制侧缺省」**是否恰当 ✓ —— 它在旧树来自 `profile.mm_struct_stride(MM_STRUCT_SZ)` ✓，即**有 profile 入口** ⇒ 应归「设备相关（补键 ✓）」还是「内核几何（常量 ✓）」？
