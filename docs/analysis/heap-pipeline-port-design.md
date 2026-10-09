# 堆/页管线移植设计（43499 的页准备）· v3

> 分析类（≤8 KiB）· **L 级**（攻击关键路径）· 作者 Lead · v1 评审见 design-critic（0 阻断 / 重要 4）
> 目标：让**重写后的** 43499 在**真机**上能做完页准备 ⇒ 「两 backend 真机可用」的第 1 优先级 ✓

## 0 沿革

v1 → v3 的逐条改动（H1–H5、K1–K4、三问判定）见附件 [heap-pipeline-port-design-history](heap-pipeline-port-design-history.md) ✓ —— 本件只留结论 ✓。

**主路径**

| 行 | 步骤 | 性质 |
|---|---|---|
| 449-450 | `close_reclaim_sockets()` + `cleanup_page_prepare_state()` | 决定 |
| 453 | `prepare_ctxs()` | 决定（几何） |
| 455-456 | **skb 缓冲分配 + 清零** | 决定（尺寸来自几何） |
| 458-461 / 463-466 | pre / spray 组：`clone_child()` + `open_memfd()` | 系统调用 |
| 468-470 | `KernelSnitchOwner::create(...)`（MM_ORDER / cpu_count / 碰撞数） | **独立机制** |
| 481-483 / 484 / 485-487 | pre 组**再**派生 · `clone_leak_child()` · post 组派生 | 系统调用 |
| 489-495 | pre / leak / post 的 fd 打开 | 系统调用 |
| 497-505 | **杀 pre/post/spray 子进程**（保留 post 末位 ✓） | **决定** |
| 506-583 | 泄漏循环（等命中，按 `kernelsnitch_timeout_ms` ✓） | 等待 + 判断 |
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
| **555-563**（K1） | **无碰撞超时**（`!snitch.has_collisions()`） | 同上（reset + 杀 prepare 子 + cleanup + **return 0**）—— **真机最可能发生的一条** ✓（由 `kernelsnitch_timeout_ms` 驱动 ✓） |

**非失败诊断**（不改变控制流 ✓）：`550-553` 的 leak child 退出码告警 ✓（旧树打印 ✓）。

**粒度口径**（K4 ✓）：§2.1 的 12 步是**阶段**渲染 ✓（它把 481 的 pre 再派生折进 `PreChildren` ✓）；**阶段内**「派生 → 开 fd → 杀子」的次序是其内部机制 ✓ ⇒ 编排必须保序 ✓。

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
| **内核几何**（K2 + L1/L2 修正 ✓） | 见下方「两个量」与「两条拒绝路」 | **机制常量 + 运行时校验** ✓ | 不适用（常量 + 校验 ✓） |

**两个量，两个名字（L1 ✓）**（不许同名 ✗ —— 本仓反复出过这类事故 ✓）：

| 量 | 含义 | 用途 | 来源 |
|---|---|---|---|
| `kernel_page_size` | 设备**内核页尺寸**（4 KiB / 16 KiB ✓） | **校验用** ✓ | seam 实测 ✓ |
| `align_region_size` | = `kernel_page_size << MM_ORDER` ✓ | **向下取整用** ✓（`align_base` ✓） | 由上一量派生 ✓ |

⇒ 校验写成 **`align_region_size == kernel_page_size << MM_ORDER`** ✓（方向不再含糊 ✓）；落地件 `page_geometry.hpp` 的字段已改名 **`align_region_size`** ✓ 并在注释里并列写明两量与关系 ✓。

**两条拒绝路，两个名字（L2 ✓）**：

| 情形 | 拒绝 | 分工 |
|---|---|---|
| **值非法**（0 / 非 2 的幂 / 越界 ✓） | **`param-invalid`** ✓ | 落地件现状 ✓（`geometry_is_usable` ✓） |
| **缺失或不一致**（键缺 ✓ / 与设备页尺寸不符 ✓） | **`geometry-missing`** ✓ | **与常量同批落地** ✓；其证伪 = seam 的 `page_size()` 返回 8192 而常量按 4096 派生 ⇒ 必须 `geometry-missing` ✓，控制组 4096 ⇒ CHECK=0 ✓ |
| **mm_struct 尺寸**（K3 裁定 ✓ · 随内核配置变化 ✓） | `backend.cve_2026_43499.abi.mm_struct.size` ✓（沿既有 `abi.*` owner 路径 ✓）· **删除兜底** ✓（旧树 `mm_struct_stride(fallback)` 的形态随旧政策作废 ✓ —— 实测某 profile 声明 `0x580` 而兜底 `0x500` ✓） | 缺键 ⇒ **`geometry-missing`** ✓（几何类 ✓；与调参类 `tuning-missing` 的分工 = 几何/偏移 vs 数量/超时 ✓） |

**派生**：`mm_objs_per_slab = ORDER3_SIZE / mm_struct_size` ✓（由该键驱动 ✓，见 `reclaim_plan` 的 slab 步长 ✓）。

## 3 真机判据（分步 ✓）

| 步 | 判据 |
|---|---|
| 决策面 | **主机**：数量/对齐/回收顺序/形状参数与旧值**逐值相同** ✓（黄金值固化 ✓） |
| seam | **真机单探针**：`open_memfd` 真开真关 ✓ · `socketpair+sendmsg` 真发 ✓ ⇒ 每条打印 errno ✓ |
| 编排 | **真机**：跑一次 ⇒ 打印每组实际数量（== profile 值 ✓）· snitch 是否命中 ✓ · 回收是否按序 ✓ · `base & (ORDER3_SIZE-1) == 0` ✓；失败给**具名**原因 ✓ |
| 对拍 | **旁证**（非门禁 ✓）：同机同档跑旧二进制，比数量/命中/耗时量级 ✓ |

## 4 反面情形（明说不做）

- ✗ 不抄旧树全局状态；✗ 不加实验用 CLI 旁路；✗ 不猜几何（缺键即拒 ✓）；✗ 不一次性移植完（决策面 → seam → 编排，逐步真机可验 ✓）；✗ seam 内不做决策；✗ 不复制步骤顺序（权威唯一 ✓）。

## 5 已裁定（无未决项）

- `mm_struct` 尺寸/stride ⇒ **数据面（profile 键）** ✓（评审裁定 ✓）：见 §2.5 第三行 ✓（键名 · 删兜底 · 缺键 ⇒ `geometry-missing` ✓）。
