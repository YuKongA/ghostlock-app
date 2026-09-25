# Extractor HOCON 输出改造计划（2026-09-24）

> 目标：extractor 新增 `--format conf`，直接输出**现有 GLK profile HOCON**（键名/结构与
> `app/src/main/assets/kernel_profiles/*.conf` 一致）；App 切换到该格式。
> `--format json` 与 `LegacyProfileConverter` **保留**，只服务旧版 extractor 的 JSON 导入。

## 背景

- 当前 `--format json`（`report.rs::build_report`）输出 legacy 形状：`release` /
  `kimage_text_base` / `kernel_phys_load` / `pselect_waiter_shift` / `symbols.off_*` /
  `struct_fields.task_*` / `btf_size`。
- App `AndroidGhostlockRepository` 以 `--format json` 调用并把 JSON 原样存为用户文档；导出/编辑时
  才由 `LegacyProfileConverter` 转成 HOCON。
- 新流程：extractor 输出即 profile（HOCON），存储/导出不再经过 legacy 转换。

## 目标输出形状（对齐现有格式）

```hocon
# GhostLock kernel profile: <release> (HOCON).
release = "<release>"
schema_version = 1
kernel_major = <5|6>
recommend_shizuku = 0            # App 策略字段；extractor 固定 0（不决定推荐）
kernel_phys_load = 0x<phys>      # 可推导时输出
route { <route> { <geometry> } } # --route 指定（含派生几何时）
fallback { to = "none" }
kernelsnitch { collisions = <n>, mm_struct_sz = <n> }
task_struct { prio = ..., ... }
cred { ... }
offset { init_task = ..., ... }
```

## 字段来源映射

| 输出键 | extractor 来源 | 现状 |
|---|---|---|
| `release` | `BootImage::release()` | ✅ 已有 |
| `kernel_major` | release 第一段 | ✅ 新增（简单） |
| `kernel_phys_load` | xbl FDT / `--phys` / MTK 推导 | ✅ 已有（JSON 同名字段） |
| `offset.*` | `symbols.off_*`（去 `off_` 前缀） | ✅ 已有 |
| `task_struct.*` | BTF `task_*`（去 `task_` 前缀） | ✅ 已有；5.15 依赖 BTF 兼容性 |
| `kernelsnitch.mm_struct_sz` | BTF `mm_struct` size | ✅ 已有（`struct_mm_struct`） |
| `route.select_stack.waiter_shift` | pselect 推导 | △ 5.15 推导失败（适配中） |
| `route.multicast_waiter.*` | 无 | ❌ 现有值来自内置/旧工具 |
| `route.tcp_zerocopy.compact_waiter` | 无 | ❌ 6.1 特例 |
| `cred.*` | 无 | ❌ 内置 `credential-6x.conf` / 5.x 手工值 |
| `kernelsnitch.collisions` | 无 | ❌ 内置 `kernelsnitch-6x.conf` / 手工 |
| `schema_version` | 固定 `1` | ✅ 新增 |

## 改造点

| 位置 | 动作 |
|---|---|
| `tools/extract_rs/src/report.rs` | 新增 `render_conf()`（键名/结构与现有 profile 对齐；offset 十进制、phys `0x`）；`--format` 增加 `conf` |
| `tools/extract_rs/src/main.rs` | `--format conf` 分支；`--route <name>`（可选）决定是否写 route 分支/几何 |
| App `AndroidGhostlockRepository` | extractor 参数改 `--format conf`；`parseEntries` 已支持 HOCON；存储即 profile |
| 测试 | Rust：`conf` 输出可被现有 schema 解析、键集与同类内置 profile 一致（golden）；App：parse→export 无转换路径 |
| 兼容 | `--format json` / `LegacyProfileConverter` 保留（旧 extractor 导入） |

## `--analysis`（已实现，2026-09-24）

> 回答"开发者怎么知道用哪条 route"：先落地从内核证据推断的能力，`--route` 只作覆盖。

`ghostlock-extract <image> --analysis` 输出只读报告（不写 offsets）：

- **release 提取修复**：跳过 `Linux version %s` 之类的格式串，取真实 `major.minor.patch-…` banner
  （A301SO 5.15 之前被命中为 `%s`，导致 family/major 全退化）。
- 报告：release/kernel_major、family（verified/unverified）、`kernel_phys_load`（含来源
  xbl / `--phys` / MTK）、`rt_mutex_waiter` 字段（`tree`/`tree_entry` …）+ size、mm_struct size、
  primitive（`remove_waiter` 修补状态）、pselect 推导结果、三条路径的符号探测
  （`core_sys_select`/`futex_wait`、`tcp_zerocopy_receive`、`ip_mc_msfadd`/`ip_mc_source`）、
  **建议 route + 置信度 + 理由**。
- 建议规则：pselect 可推导 → `select_stack`(high)；5.x 且 multicast 路径存在 → `multicast_waiter`(medium)；
  6.1 且 tcp 路径存在 → `tcp_zerocopy`(medium)；否则 family 默认(low)；未知且无推导 → none。
- 实测（A301SO 5.15 `boot.img`）：`suggested route multicast_waiter (medium)`——与内置 profile 一致。
- 据此，开放问题 1 的 route 来源 = **分析建议 + `--route` 覆盖**；`--format conf` 写 route 分支时复用
  同一判定（或读取 `--route`）。

## 开放问题（待确认）

1. **route 来源**：分析建议（`--analysis`，基于 waiter 布局/pselect/路径证据）+ `--route` 覆盖（推荐，
   已实现建议部分）；route 几何输出等 5.15 推导适配后再接。
2. **cred / kernelsnitch.collisions**：extractor 是否输出 `include "credential-6x.conf"`（6.x）或内联
   （5.x）？推荐：extractor 只输出它推导的字段，`cred`/`collisions` 由 App 合并内置资产。
3. **顺序**：先落格式（本计划），再做 5.15 推导（family/slab/shift/mcast）？
4. **数值格式**：与内置一致（`offset` 十进制、`kernel_phys_load` 用 `0x…`）→ 推荐。

## 进度

- [x] 现状定位：`report.rs::build_report`（JSON 输出）、App `--format json` 调用、`LegacyProfileConverter`。
- [ ] 用户确认开放问题（推荐默认见上）。
- [ ] 实现 `render_conf` + CLI + App 切换 + 测试。
