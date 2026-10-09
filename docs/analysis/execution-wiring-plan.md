# 43499 执行体接线计划（批次计划 · 把已就位的链送进生产路径）

> 分析类（≤8 KiB）· **L 级**（攻击关键路径）· 作者 Lead · 依据 = 退休树逐件行数（`git show b0e9f499^:…` ✓）

## 1 现状（已就位 ✓ / 缺什么 ✗）

**已就位**（新树，真机或主机已证 ✓）· **归档**：搜索与 seam 的真机证据在 `docs/analysis/device-gates/snitch-search-20261008-pass.md` ✓（本轮由评审 M2 要求补 ✓）：地址契约 ✓ · 扫描界/几何 ✓ · 候选规则 ✓ · futex 哈希（定向量复现 ✓）· 走查顺序 ✓ · 区域布局 ✓ · 搜索 seam（真机 ✓）· 唤醒协议（真机 ✓）· 子进程 poke（真机 ✓）· 整体搜索（**真机端到端 ✓**）· 调参读取+直达 ✓ · **搜索↔页准备接合 ✓** · 页准备决策面 6 件 + seam（真机 ✓）+ 编排（假平台验过失败路径 ✓）· 两个 PI-futex 原语（真机 ✓）。

**缺**（退休树里有、新树没有 ✗）：

| 件 | 行数 | 新树对应 |
|---|---|---|
| `route/route_policy.hpp` | 257 | **route 策略形状** ✗（三条 route 的共同面 ✓） |
| `route/select_stack_route.cpp` | 532 | route 之一 ✗ |
| `route/tcp_zerocopy_route.cpp` | 372 | route 之一 ✗ |
| `route/multicast_waiter_route.cpp` | 208 | route 之一 ✗（**其数据面已在** ✓ `memory/multicast_arm` ✓） |
| `steps.cpp` | 449 | **W1/W2/W3 执行体** ✗（数据面已在 ✓ 凭据镜像/写布局/写请求/编码器 ✓） |
| `primitives.cpp` | 202 | 写原语的**执行面** ✗（`write_*` 决策 ✓ 已就位 ✓） |
| `terminal/root_child.cpp` | 105 | 终端 ✗（A6-4 载荷件已就位、未接 ✓） |
| `bootstrap.cpp` | 126 | 引导/wiring ✗ |
| `schema.hpp` + `glkv3_schema.hpp` | 221+114 | **键权威表** ✗（新树现有 `key_catalog` ✓ 只覆盖 **manifest 导出** ✓；退休树这份管**校验** ✓ ⇒ 两者关系待定 ✓ 见 §4） |

## 2 依赖顺序（每步都以真机为判据 ✓）

```text
① route 形状 + 一条 route（建议 multicast ✓ —— 其数据面已就位、且旧树三条中最小 208 行 ✓）
② route → 页准备：route 提供 ps_page_size / mm_slab_order / 组数 ✓（编排已接 ✓）
③ primitives 执行面（把 write_* 的决策落到 seam ✗ ⇒ 需第三条 seam：写 seam ✓）
④ steps（W1 SELinux → W2 凭据/uid0 → W3 seccomp，按 `contract` 的 stepset 顺序 ✓）
⑤ 终端（root_child ✓ 载荷件已就位 ✓）
⑥ bootstrap：从文档 → 选择 → route → 搜索 → 页准备 → steps → 终端 的接线 ✓
```

**每步的真机判据**（沿用既有形态 ✓）：探针打印该步的具名结果 + 具名拒绝 ✓；攻击路径改动 = 真机门禁 + `docs/analysis/device-gates/` 记录 ✓。

## 3 与既有裁定的关系（不得与之冲突 ✗）

- **方案 C**：脚本是**唯一的顺序权威** ✓ ⇒ ④ 的 steps 顺序**必须来自脚本/内置脚本** ✓ 而不是 C++ 里的顺序表 ✓（`prepare_steps` 先例：顺序写成数据 + pin ✓）。
- **裁定ⓒ**：设备相关量 ⇒ profile 键 ✓；内核几何 ⇒ 机制常量 ✓（route 的几何按此分类 ✓）。
- **Q2 rule 1**：C++ 不得内嵌数值 ✓ ⇒ route 的 532/372/208 行里**凡是几何/时序**都要落到 profile ✓（这是本次移植**最大的风险面** ✗，见 §4）。

## 4 裁定（评审 v0.2 · 2026-10-08 · 0 阻断）

1. **先 multicast ✓**（三条依据，比原稿更强）：① 最小（**208 行** ✓ 实测）；② 数据面已就位（`memory/multicast_arm` ✓）；③ **设备证据最多** —— `device-gates` 按 route 名命中数实测 **multicast_waiter 22 / select_stack 1 / tcp_zerocopy 0** ✓ ⇒ 原稿「select_stack 最常被真机验证」的前提**不成立且方向相反** ✗（已删）。另：`execution-multicast-waiter.conf` **已在**（提交 `7b34bb44` ✓）⇒ 无阻塞 ✓。
2. **分工，不合一 ✓**（判据 = **分层防火墙**，非偏好）：`route/expected_keys.hpp:5-11` 明写「kinds 用本地枚举 **使本层不依赖 profile 层**」✓ ⇒ 把校验并进 profile 层的 `key_catalog` 会让路由层**反向依赖** ✗。正确形态 = **清单（`key_catalog` = 导出权威）× 期望（各组件自声明）**，由**组合根**匹配 ✓；退休的 `schema.hpp`+`glkv3_schema.hpp`（221+114 ✓）**不照搬形状** ✗，按「组件自声明期望」重写，键全集只留 `key_catalog` 一处 ✓。
3. **采纳堆管线同一形态 ✓**（决策面 → seam → 编排 → 真机探针 ✓），加两条硬边界：① **seam 只含 syscall** ✓（不含决策/组数/顺序/超时 ✓，每条打印 errno ✓ —— 与 `futex_seam_linux.hpp` 的「Nothing here decides anything」同义 ✓）；② **写 seam 的清单必须自退休文件逐条枚举** ✓（见 §5 ✓）。

**同批判据（评审要求 ✓）**：补一条**对拍断言** —— 「每个已接线 route 的期望键 ∈ `key_catalog` 且 kind 兼容」✓（否则仍是两份名单 ✗）。

## 5 ③ 的前置检查项（M3 ✓：退休文件里的裸数值与 syscall 逐条枚举）

评审实测退休 `primitives.cpp`（202 行 ✓）至少含：

**全文枚举（202 行 ✓ 逐处 ✓ · 评审要求「不只上列 5 处」✓）**：

| 行 | 内容 | 归类 | 落点 |
|---|---|---|---|
| :37/:42/:56 | `for (wave < waves)` · `for (i < batch)` · `terminate_and_wait(SIGKILL)` | 次数来自输入 ✓ / 信号 = 机制 ✓ | 次数 ⇒ profile ✓ · `SIGKILL` ⇒ 机制常量 ✓ |
| :58 | `usleep(20000)` | **裸数值** ✗ ⇒ 时序 | **profile 键** ✓ |
| :68 | `pe.sample_period = 5000` | **裸数值** ✗ ⇒ 采样周期 | **profile 键** ✓ |
| :77 | `syscall(__NR_perf_event_open, …)` | **系统调用** | **写 seam** ✓ |
| :82 | `constexpr size_t kPerfPageSize = 4096` | 内核几何（perf 页尺寸 ✓） | **机制常量 + 注释** ✓ |
| :86 | `mmap(…, MAP_SHARED, fd.get(), 0)` | **系统调用** | **写 seam** ✓ |
| :94 / :96 | `ioctl(PERF_EVENT_IOC_ENABLE / DISABLE)` | **系统调用** | **写 seam** ✓ |
| :95 | `for (i < 500000) syscall(__NR_getpid)` | **裸数值** ✗ ⇒ 时序 | **profile 键** ✓ |
| :102 | `std::array<uintptr_t, 256> cands` | 容量（机制 ✓） | **机制常量 + 注释** ✓ |
| :104/:115/:130 | `while (pos < head …)` · `for (i < 32 …)` · `for (i < nc)` | 前两者**裸数值** ✗（投票窗 32）/ 第三来自输入 ✓ | 32 ⇒ **profile 键** ✓ · 其余 ⇒ 输入 ✓ |
| :146 | `attack_write` 写路径 | **混合** | 决策面已有（`write_*` ✓）+ 系统调用 ⇒ **写 seam** ✓ |

**小结**：写 seam 需含 `perf_event_open` / `mmap` / `ioctl(ENABLE|DISABLE)` / `getpid`（+ 写路径自身的系统调用 ✓，动工时逐条补 ✓）；**四个裸数值**（`20000` · `5000` · `500000` · `32`）全部 ⇒ **profile 键** ✓；两个机制常量（`4096` perf 页 ✓ · `256` 候选容量 ✓）⇒ 进代码 + 注释 ✓。

**判据（可执行 ✓）**：新树对应件内**不得出现裸数值** ✗ ⇒ grep 判据（`[0-9]{3,}` 逐处须有键引用或机制常量注释 ✓）；本枚举表在 ③ 动工前**逐条补全**（202 行全文 ✓ 不只上列 5 处 ✓）。

> 件类（M5 ✓）：本件是**后继批次计划**（不是 heap-pipeline-port v3 的沿革附件 ✓）；与之互指：设计与沿革见 `heap-pipeline-port-design.md` + 其附件 ✓；本轮的搜索真机证据见 `device-gates/snitch-search-20261008-pass.md` ✓。

## 6 搜索的输入从哪里来（2026-10-08 实测 ✓）

| 输入 | 来源 | 现状 |
|---|---|---|
| 表尺寸 | **profile 键** ✓ | 资产 `execution-tuning.conf` 已有（`snitch_wake_rounds` 等 ✓）；表尺寸按 256 使用 ✓ |
| 桶规则 | **推导** ✓（内核 futex 哈希 ✓） | 已落 `leak/collision_buckets.hpp` ✓ + 真机跑通 ✓ |
| 证明地址 | **推导** ✓（同桶者 ✓） | 不在 profile 里 ✓ ⇒ 由哈希+表尺寸+`mm` 推出 ✓ |
| 范围 | **机制**（直接映射切片 ✓） | 边界取自设备 ✓；探针用切片 ✓，生产范围另定 ✓ |
| `mm` | **搜索要找的那个对象** ✓ | 循环依赖已解：准备把已知 `mm` 交给搜索 ✓，搜索返回对象 ✓ |
| **`mm_struct_sz`** | **profile 键（数据面）** ✓ | **`null`** ✗ ⇒ **具名缺口** ✓（`kernelsnitch-6x.conf` 里字面写 `mm_struct_sz = null` ✓） |

**关于 `mm_struct_sz = null`** ✓（评估此缺口的确切代价 ✓）：它属**数据面**（按裁定 ✓：`backend.cve_2026_43499.abi.mm_struct.size` ✓），
**搜索不需要它** ✓（搜索只用哈希 ✓），**载荷构造需要它** ✓ ⇒ 它阻塞的是**写路径**而非搜索 ✓ ⇒
⇒ 该值的取得必须是**测量**（不得猜 ✓）：从设备按 `uname -r` 对应的结构布局量出 ✓，或由 ABI 面导出 ✓。

**下一步**（本批）：把探针的范围换成**设备直接映射的切片** ✓（而非从 0 开始 ✓），使候选来自真实地址空间 ✓；
桶与表尺寸已就位 ✓ ⇒ 届时「找到的」才是**真实地址上的**同桶者 ✓，而不是地址 0 ✓。
