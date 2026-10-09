# 43499 执行体接线计划（把已就位的链送进生产路径）

> 分析类（≤8 KiB）· **L 级**（攻击关键路径）· 作者 Lead · 依据 = 退休树逐件行数（`git show b0e9f499^:…` ✓）

## 1 现状（已就位 ✓ / 缺什么 ✗）

**已就位**（新树，真机或主机已证 ✓）：地址契约 ✓ · 扫描界/几何 ✓ · 候选规则 ✓ · futex 哈希（定向量复现 ✓）· 走查顺序 ✓ · 区域布局 ✓ · 搜索 seam（真机 ✓）· 唤醒协议（真机 ✓）· 子进程 poke（真机 ✓）· 整体搜索（**真机端到端 ✓**）· 调参读取+直达 ✓ · **搜索↔页准备接合 ✓** · 页准备决策面 6 件 + seam（真机 ✓）+ 编排（假平台验过失败路径 ✓）· 两个 PI-futex 原语（真机 ✓）。

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

## 4 待评审问题（3 条）

1. **顺序**：先 multicast（最小、数据面已就位 ✓）是否恰当 ✓？还是应先做 `select_stack`（旧树三条中最常被真机验证的一条 ✗ hmm: 需核 ✓）？
2. **两份 schema 的关系**：新树 `key_catalog`（manifest 导出 ✓ 已生成 ✓）与退休树 `schema.hpp`（校验 ✓）是否应**合一**（一处权威、两个用途 ✓），还是**分工**（导出 vs 校验 ✓）？合一更省心 ✓ 但改动面更大 ✓。
3. **第三条 seam（写 seam）** 的边界：`primitives.cpp` 的 202 行里，哪些是决策（⇒ 已有 `write_*` ✓）哪些是系统调用（⇒ 新 seam ✓）—— 是否应按堆管线同一形态（决策面先落 + seam + 编排 + 真机探针 ✓）？
