# Extractor HOCON 输出改造计划（2026-09-24）

> 目标：extractor 新增 `--format conf`，直接输出**自包含的 GLK profile HOCON**（键名/结构与
> `app/src/main/assets/kernel_profiles/*.conf` 一致）；App 切换到该格式。
> 输出为 **flatten**：不写 `include`，`credential-6x.conf` / `kernelsnitch-6x.conf` 等共享段
> 内联展开，输出必须带 `cred { ... }` 段，单文件即可用。
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
# GhostLock kernel profile: <release> (HOCON, self-contained).
release = "<release>"
schema_version = 1
kernel_major = <5|6>
recommend_shizuku = 0            # App 策略字段；extractor 固定 0（不决定推荐）
kernel_phys_load = 0x<phys>      # 可推导时输出；不可推导则省略
route { <route> { <geometry> } } # --analysis 建议 + --route 覆盖（几何可推导时）
fallback { to = "none" }
kernelsnitch {                   # 6.x 内联 kernelsnitch-6x.conf 常量
  collisions = 4
  mm_struct_sz = 1024            # 仅 6.1 实测 SLUB stride；其余 family 省略（native 默认）
}
task_struct { prio = ..., ... }
cred {                           # 6.x 内联 credential-6x.conf 常量
  caps_offset = 48
  copy_size = 136
  usage_value = 1
  caps_count = 5
  caps_value = -1
}
offset { init_task = ..., ... }
```

flatten 规则（实现时必须遵守）：

- 不输出 `include` 行；`credential-6x.conf` 与 `kernelsnitch-6x.conf` 的内容直接内联。
- 保持现有嵌套块风格与键名（不是点键）；不使用 `null`，无值的键直接省略。
- 6.x 必须有 `cred` 段（常量内联）；5.x 输出 BTF 可推导的 `cred` 键（`caps_offset` 已有
  `cred_caps`，`copy_size` 用 `sizeof(cred)`），`usage_value`/`caps_value`/`ref_*` 待适配后补。

## 字段来源映射

| 输出键 | extractor 来源 | 现状 |
|---|---|---|
| `release` | `BootImage::release()` | ✅ 已有 |
| `kernel_major` | release 第一段 | ✅ 新增（简单） |
| `kernel_phys_load` | xbl FDT / `--phys` / MTK 推导 | ✅ 已有（JSON 同名字段） |
| `offset.*` | `symbols.off_*`（去 `off_` 前缀） | ✅ 已有 |
| `task_struct.*` | BTF `task_*`（去 `task_` 前缀） | ✅ 已有；5.15 依赖 BTF 兼容性 |
| `kernelsnitch.mm_struct_sz` | 仅 6.1 写实测 `1024`（= 现有 `build_report` 特例）；其余 family 省略 | ✅ 已有 |
| `route.select_stack.waiter_shift` | pselect 推导 | △ 5.15 推导失败（适配中） |
| `route.multicast_waiter.*` | 无 | ❌ 现有值来自内置/旧工具 |
| `route.tcp_zerocopy.compact_waiter` | 无 | ❌ 6.1 特例 |
| `cred.*` | 6.x：`credential-6x.conf` 常量内联；5.x：BTF 推导（适配中） | 🔶 决定：flatten 后必须内联，不得 include |
| `kernelsnitch.collisions` | 6.x：常量 `4`；5.x：暂无 | 🔶 决定：同 cred，flatten 内联 |
| `schema_version` | 固定 `1` | ✅ 新增 |

路由来源：`--analysis` 建议 + `--route` 覆盖（用户已确认，见下）。

## 改造点

| 位置 | 动作 |
|---|---|
| `tools/extract_rs/src/report.rs` | 新增 `render_conf()`（flatten：无 `include`，6.x 凭据/探针常量内联；键名/结构与现有 profile 对齐；offset 十进制、phys `0x`）；`--format` 增加 `conf` |
| `tools/extract_rs/src/main.rs` | `--format conf` 分支；`--route <name>`（可选）决定是否写 route 分支/几何 |
| App `AndroidGhostlockRepository` | extractor 参数改 `--format conf`、生成文档名 `.conf`；`parseEntries` 支持 HOCON；存储即 profile（v2，无 legacy 转换） |
| 测试 | Rust：`render_conf` 单测（flatten/无 include/cred 常量/6.1 特例/5.x BTF 部分 cred/route 几何）；App：`UserProfileStoreTest` 断言 extractor conf parse→export 不被转换或注入 |
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

## 决定与开放问题

已决（2026-09-24 用户）：`--format conf` 的输出是 **flatten + 包含 `cred`** 的自包含 HOCON，
不写 `include`；6.x 内联 `credential-6x.conf` / `kernelsnitch-6x.conf` 常量，单文件即可用。

1. **route 来源**：`--analysis` 建议（基于 waiter 布局/pselect/路径证据）+ `--route` 覆盖（建议部分
   已实现）；route 几何输出等 5.15 推导适配后再接。
2. **cred / kernelsnitch.collisions**：~~extractor 是否 include~~ → 已决：flatten 内联，见上。
3. **顺序**：先落 conf 格式（本计划），再做 5.15 推导（family/slab/shift/mcast/cred）→ 采用。
4. **数值格式**：与内置一致（`offset` 十进制、`kernel_phys_load` 用 `0x…`）→ 采用。

## 进度

- [x] 现状定位：`report.rs::build_report`（JSON 输出）、App `--format json` 调用、`LegacyProfileConverter`。
- [x] 用户确认：输出 flatten、包含 `cred`（2026-09-24）。
- [x] 实现 `render_conf` + `--format conf` / `--route` + App 切换 + 测试（2026-09-25）。
- [ ] 5.15 multicast 几何 / 凭据 `ref_*` 推导（下一批）。

## 实现与验证记录（2026-09-25）

- Rust：`cargo test --manifest-path tools/extract_rs/Cargo.toml` → 16 passed；
  `cargo clippy --all-targets` 新增 0 warning；新增代码通过 `rustfmt --check`（文件剩余差异为既有）。
- App：`./gradlew :app:testDebugUnitTest :profile-core:test exportKernelProfiles` → BUILD SUCCESSFUL。
- 产物形状：无 `include`；6.x `cred` 5 键与 `kernelsnitch.collisions = 4` 内联；6.1 `mm_struct_sz = 1024`；
  5.x `cred` 取 BTF `caps_offset` / `copy_size`；几何不可推导的 route 不写分支（警告，由内置 profile 补）。
- route 来源：`--analysis` 建议（`analysis::suggest_route`，与报告同一实现）+ `--route` 覆盖；
  几何：select_stack 用推导/启发式 `pselect_shift`，tcp_zerocopy 仅 6.1 写 `compact_waiter = 1`。
- 文档：README(_ZH)、PROFILE_SCHEMA(_ZH)、kernel_profiles/README(_ZH)、engineering-standards 同步
  `--format conf` 与 v1 边界。
- 未触及 `src/core` 攻击路径，故不需要 `cmp_disasm` / 真机门禁。
