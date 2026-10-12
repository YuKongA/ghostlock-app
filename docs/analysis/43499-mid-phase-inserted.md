# 43499 · 中间 groom 已插入（默认关闭）+ 接入配方（round 142）

## 已完成（代码在树里 ✓，默认不生效 ✓）

```cpp
// prepare_plan.hpp：两个结构各加一个字段 ✓
std::size_t pre_shaped_groups = 0U;   // 退役 pre_ctx 的对应物
// prepare_plan.cpp：原样搬运 ✓
out.pre_shaped_groups = inputs.pre_shaped_groups;
// prepare_page.hpp：插在 spray **之后**、snitch **之前** ✓（复用 pre 数组与 state.pre 计数 ⇒ 关闭路径无需改 ✓）
for (std::size_t i = 0; i < plan.pre_shaped_groups; ++i) { …fork / exit_child / 记账… }
```

- 构建：android RC=0 ✓；默认值 0 ⇒ **与既有行为逐字相同** ✓（安全 ✓）；
- 位置与退役 `pre_ctx` 一致 ✓（`spray.cpp:272-281` ✓：`mm_objs_per_slab − 1` = 31 ✓）。

## 接入配方（下一步，4 处 ✓）

| # | 文件 | 改动 |
| --- | --- | --- |
| 1 | `steps/prepare_mapping.hpp` | 表头加一行（短名 "pre_shaped_groups" ✓、文档键 "backend.cve_2026_43499.execution.heap.pre_shaped_groups" ✓、拒绝名 "prepare-value-missing:pre_shaped_groups" ✓）；`values` 结构加 `std::uint64_t pre_shaped_groups = 0U;` ✓ |
| 2 | `steps/prepare_mapping.cpp` | 项表加一行（与 `pre_groups` 同形 ✓） |
| 3 | `steps/prepare_runner.cpp` | 把 `values.pre_shaped_groups` 拷进 `inputs.pre_shaped_groups` ✓（与 `values.pre_groups` 同处 ✓） |
| 4 | `profile/profile-schema.json` | `execution.heap` 加键 `pre_shaped_groups` ✓（schema 是人工维护的权威 ✓） |

⇒ 之后夹具设 `pre_shaped_groups = 31` ✓ ⇒ 判据：**连续 3 次**跑无 `payload-layout-not-accepted` ✓。
