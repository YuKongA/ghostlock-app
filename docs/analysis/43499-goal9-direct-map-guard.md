# 43499 · 新目标第 9 轮：退役的直映射判据（逐字取得，待落码）✓✓

## 一、退役的两条守卫（✓，逐字 ✓）

```cpp
// ① steps.cpp:70（retry_write_stage 的**首行** ✓）
if (!memory::in_direct_map(target)) {
    pr_warning("%s: target 0x%016zx is outside the direct map, not attempting\n", stage, target);
    return false;                       // ⇒ **不尝试** ✓（"no attempt can move a target that is
}                                       //    wrong by construction" ✓）

// ② primitives.cpp:152（写入原语内 ✓）
if (!memory::in_direct_map(request.target)) { … }

// ③ 判据本体（memory/direct_map.hpp:13 ✓）
extern std::uint64_t g_direct_map_end;                  // "built-in or narrowed by a measured iomem dump" ✓
inline std::int32_t in_direct_map(std::uintptr_t t) { return t > DIRECT_MAP_BASE && t < g_direct_map_end; }
```

## 二、当前树的现状（✓）

```
· `g_direct_map_end` / `DIRECT_MAP_BASE` / `in_direct_map` **都不在**本树 ✗（AGENTS §4 只把它当约定提到 ✓）；
· `direct_map` 字样只出现在三处（script_surface.hpp ✓ / perf_vote.hpp ✓ / scan_geometry.hpp ✓）⇒ 非本判据 ✓；
· 我的步骤体在 `main.cpp:792` 打印 `main: step=%s target=0x%llx …` ✓ ⇒ **加 in/out 列最省事** ✓。
```

## 三、真机观察到的目标（✓，供比对 ✓）

```
w1 target=0xffffff802adaad88 ✓（= 归档 W1 ✓，在直映射族内 ✓）
w2 target=0xffffff89ac57cf98 ✓（受害 task + cred ✓，同族 ✓）
w3 target=0xffffffc03f313eb0 ✗ ← **超出 0xffffffc000000000**（39 位 VA 的直映射上界 ✓）
   ⇒ 若 39 位 VA ⇒ 该地址落在 **vmalloc/模块区** ✗ ⇒ 退役会**拒绝尝试** ✓；
   ⇒ 这可能正是 w3（seccomp）不生效的原因 ✓，且 w2 的同类地址需逐个核 ✓。
```

## 四、下一步（✓，可直接落码 ✓）

1. 在 `main.cpp:792` 的打印里加 `in=0/1` 列 ✓（用与退役同形的判据 ✓，边界从当前树的既有常量取 ✓）；
2. 按退役语义对 `in == 0` 的步骤给**具名拒绝** ✓（而非硬写 ✓）；
3. 真机判据：三条目全 `in=1` ✓ 且任一轮 `victim uid=0` ✓。

## 五、门禁（✓，本轮未改码 ✓）

```
check → 108 PASS ✓；android → 0 警 ✓；lint → 0 ✓
```
