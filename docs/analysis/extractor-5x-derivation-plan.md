# Extractor 5.x 推导改造计划（2026-09-25）

> 目标：`--format conf` 对 5.x（当前唯一验证设备 5.15 A301SO）输出完整凭据模板、multicast
> 几何与 BSS/零页偏移，不再依赖内置 profile 兜底。推导规则已用 A301SO `boot.img` 离线复现。

## 背景

- 上一批（`extractor-hocon-output-plan.md`）落地 flatten conf；5.x 目前只输出 BTF 可推的
  `cred.caps_offset`（`cap_inheritable`）与 `cred.copy_size`，其余 cred 字段、multicast
  几何、`offset.empty_zero_page`、`offset.mcast_fake_bss` 留空，由内置 5.15 profile 合并补齐。
- 历史推导记录（Codex share，本机会话 + `repro/xperia-first-success` patch）说明了各值的来源；
  本机用 `tools/extract_rs` 对 A301SO 67.2.A.3.178 `boot.img` 复现了全部规则。

## 证据（2026-09-25 本机复现）

`init_cred`（镜像偏移 = kallsyms `init_cred` - `_text` = 0x2bfd588）的字节 dump：

```
000: 0x0000000000000004   # usage 原值（攻击抬高值 256 是常量）
028: 0x0000000000000000   # cap_inheritable = 0
030: 0x000001ffffffffff   # cap_permitted ┐
038: 0x000001ffffffffff   # cap_effective ├ 连续非零 cap 槽 ×3
040: 0x000001ffffffffff   # cap_bset     ┘
048: 0x0000000000000000   # cap_ambient = 0
078: 0x0000000000000000   # security = 0
080: 0xffffffc00ab23a80   # user        ┐
088: 0xffffffc00acce110   # user_ns     ├ 4 个非零 canonical 指针
090: 0xffffffc00ab23ff0   # ucounts     │
098: 0xffffffc00ab23b28   # group_info  ┘
```

- 与内置 `5.15.189` profile 的 `ref0..3_image`（十进制负数）逐位一致。
- 与 git 历史 `repro/xperia-first-success/v15-relocated-private-cred.patch` 的 cred 模板逐位一致。

kallsyms 与 BTF：

- `z_pagemap_global` = 47849664 = 内置 `offset.mcast_fake_bss`（fake BSS 宿主符号）。
- `empty_zero_page` = 47529984 = 内置 `offset.empty_zero_page`。
- BTF：`cred` 176 字节，`cap_inheritable`=40、`cap_ambient`=72、`security`=120；
  `rt_mutex_waiter.task`=48、`.lock`=56（= 内置 multicast `task_offset`/`lock_offset`）。

历史教训（POCO 研究记录）：`waiter_off = 0x60` 经 IPv4 UDP probe 确认；
`fake_lock = z_pagemap_global + 0x1200`、`fake_task = fake_lock + 0x2000`、
`fake_lock2 = fake_lock + 0x80`（= 内置 `lock_slots_offset`）。

## 推导规则

### cred（5.x）

| 字段 | 规则 |
|---|---|
| `copy_size` | BTF `sizeof(cred)` |
| `caps_offset` | `cap_inheritable .. cap_ambient+8` 间第一个非零 u64 槽 |
| `caps_count` | 自 `caps_offset` 起连续等于 `caps_value` 的 u64 槽数 |
| `caps_value` | 上述槽值（5.15：`0x1ffffffffff`） |
| `usage_offset` | BTF `cred.usage`（profile 可省略，native 默认 0） |
| `usage_value` | 常量 `256`（攻击抬高引用计数，非镜像值） |
| `ref{N}_offset` / `ref{N}_image` | BTF `cred` 的 8 字节指针成员，读 `init_cred` 值；非零且 `value >> 48 == 0xffff` 入选，按偏移升序 |
| `ref_count` | 入选数量（≤ 4；多于 4 个报错，不截断） |

### multicast 几何（5.x）

| 字段 | 规则 |
|---|---|
| `task_offset` / `lock_offset` | BTF `rt_mutex_waiter.task` / `.lock` |
| `waiter_off` / `buffer_size` / `fake_lock_offset` / `fake_task_offset` / `lock_slots_offset` / `lock_slot_count` / `lock_slot_stride` | 常量 `96 / 264 / 4608 / 12800 / 128 / 12 / 8`（POCO probe + 历史验证） |
| `compact_waiter` | `1` |
| `offset.empty_zero_page` | kallsyms `empty_zero_page` |
| `offset.mcast_fake_bss` | kallsyms `z_pagemap_global` |

推导失败（无 BTF、无符号、镜像越界、ref > 4）时省略对应字段并警告；同分支合并时由内置
profile 补齐（`ProfileMerger` 同 route 分支按字段 deep-merge）。

## 改造点

| 位置 | 动作 |
|---|---|
| `tools/extract_rs/src/derive.rs` | `derive_cred_5x`（BTF + 镜像读取）、纯函数 `select_cred_caps` / `select_cred_refs`；`multicast_geometry_5x`（BTF task/lock + 常量） |
| `tools/extract_rs/src/report.rs` | `render_conf` 接收完整 cred 键值、route 几何与额外 offset 键；`conf_route_geometry` 增加 multicast 分支 |
| `tools/extract_rs/src/main.rs` | conf 分支推导并传入；`empty_zero_page` / `z_pagemap_global` 从 kallsyms 取 |
| 测试 | 纯函数单测（cap 槽、ref 筛选、geometry 常量）；离线 golden：A301SO boot.img 的 conf 输出与内置 `5.15.189` profile 逐字段一致（镜像不入库，证据记录于本文件） |

## 开放问题

1. `z_pagemap_global` 是 5.15 GKI 常见符号；无此符号的 5.x 省略 `mcast_fake_bss` + 警告。
2. 几何常量取自 5.15.189；其它 5.x 需真机复核（模板文档已声明非稳定 ABI）。
3. cred 的 `ref_*` 选择规则在其它 5.x 上可能选出多于 4 个指针；此时报错而不是截断。

## 进度

- [x] 推导证据复现（本机 A301SO `boot.img`，临时 example 已删除）
- [x] 实现 + 单测（2026-09-25）
- [x] golden 对比（与内置 5.15.189 profile 逐字段）
- [x] 真机门禁：不适用（见下"门禁判定"）

## 实现与验证记录（2026-09-25）

- `derive.rs`：`Cred5x`、纯函数 `select_cred_caps` / `select_cred_refs`、`derive_cred_5x`
  （BTF 布局 + 镜像 `init_cred` 值）、`multicast_geometry_5x`（BTF task/lock + 已验证常量）。
- `report.rs`：`render_conf` 改用 `ConfInputs`；新增 `conf_cred_5x`、`ConfExtraOffsets`；
  `conf_route_geometry` 增加 5.x multicast 分支（6.x 行为不变）。
- `main.rs`：conf 分支推导 cred/geometry；kallsyms 取 `empty_zero_page`、`z_pagemap_global`；
  推导失败时省略并警告（同分支由内置 profile 补齐）。
- 放宽 `struct_slab_cache` 为可选 struct 字段：5.15 GKI BTF 没有 `slab` 类型，此前会让 5.x 的
  JSON/conf 提取以 exit 4 失败；现在作为 `missing:` 提示但不再中断。
- 单测：`cargo test --manifest-path tools/extract_rs/Cargo.toml` → 20 passed。
- golden：A301SO 67.2.A.3.178 `boot.img --format conf` 与内置 `5.15.189` profile
  **54/57 键完全一致**；差异仅 3 处且均为计划内：
  - `kernelsnitch.collisions` / `kernelsnitch.mm_struct_sz`：5.x 无推导来源，省略（内置合并补齐）；
  - `recommend_shizuku`：既定策略固定 `0`（不决定推荐）。
- 格式门槛：新增代码通过 `rustfmt --edition 2024 --check`；`cargo clippy --all-targets`
  无新增警告（余下 3 条位于既有行）。

## 门禁判定：本批不适用真机门禁

AGENTS 的真机门禁只由**攻击关键路径改动**触发（waiter/race/payload/route/exec 流程）。本批
只改 `tools/extract_rs` 与文档，`src/core` 一行未动；且 extractor 输出与已通过真机门禁的内置
`5.15.189` profile 在攻击参数上逐字段相同（route 几何、cred 全字段、task/offset 全部一致；
`kernelsnitch` 与 `recommend_shizuku` 由内置/App 策略层补齐）。运行时 `TargetProfile` 快照与
门禁通过时无差异，再跑真机不产生新信息，只增加设备风险。

需要真机复验的条件（将来触发）：

- 以 extractor 输出**替代**内置 profile（不依赖内置合并），或用于没有内置条目的新 5.x release；
- 修改 `derive.rs` 的推导规则或 `MULTICAST_5X_*` / `CRED_5X_USAGE_VALUE` 常量；
- `src/core` 的 multicast/cred 路径本身变化。
