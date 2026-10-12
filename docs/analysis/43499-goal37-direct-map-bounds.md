# 43499 · 第 37 轮：`in_direct_map` 守卫的**界从哪来**（本树已找到数据源 ✓）

## 一、退役的界（✓，逐字 ✓）

```cpp
// memory/target.h
inline constexpr std::uintptr_t DIRECT_MAP_BASE = target::address::kDirectMapBase;   ✓
inline constexpr std::uintptr_t DIRECT_MAP_END  = target::address::kDirectMapEnd;    ✓
// memory/address_space.cpp
uint64_t g_direct_map_end = DIRECT_MAP_END;   // "can only be narrowed by a rooted /proc/iomem dump" ✓
// memory/direct_map.hpp
inline std::int32_t in_direct_map(uintptr_t t) { return t > DIRECT_MAP_BASE && t < g_direct_map_end; } ✓
```

## 二、本树的对应物（✓，第 37 轮 grep ✓）

```
· 直映射**基址**有数据源 ✓：`layout_from_document.hpp:33 p0_page_offset` ✓（来自文档 ✓）
  并用于别名公式 ✓（`:55 (physical - phys_offset) | p0_page_offset` ✓）；
· `main.cpp:557 kAliasBase = 0xffffff8000000000ULL` ✗ —— 本树也有一处硬编码基址 ✓（与退役同族 ✓）；
· **上界**在本树**没有对应物** ✗（退役的 `DIRECT_MAP_END` ✗）⇒ 需从文档取 ✓，或登记为待定项 ✓。
```

## 三、因此守卫可这样落地（✓，唯一未实现的那条 ✓）

```
① 下界：取文档的 `p0_page_offset`（或既有 `kAliasBase` ✓）⇒ **不写死** ✗；
② 上界：**文档里没有**该键 ✗（第 38 轮已查 ✓：`key_catalog` 无 `direct_map`/`page_offset`/`iomem` ✓；
   夹具 `abi` 只有 `task_struct/cred/kernel/offset` ✓）⇒ 不得凭空造数 ✗ ⇒ 改为**族判据** ✓：
   以文档的 `p0_page_offset`（实为直映射基 ✓）作**窗基**，判 `target` 与**同一窗**是否一致 ——
   即"与别名基址同族" ✓（第 10 轮我就是这样人工判的 ✓：w1/w2 目标 0xffffff8[089]… 与归档同族 ✓）；
③ 语义按退役 ✓：越界 ⇒ 打 `"target is outside the direct map, not writing"` ✓ 并**不写** ✗（具名 ✓）；
④ 判据：真机读 w1/w2/w3 目标 ⇒ 应全部**在界内** ✓（否则那条即为根因 ✓）。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 警 ✓；lint → 0 ✓
```
