# `kernel_phys_offset`（DRAM base）profile 字段计划（2026-10-01）

## 现状与基线

- 分支 `main`，基线 `2d9d801`（含 `02369c1` profile 完整字段；extractor 十进制）。
- 触发设备：MTK6893（moto edge 40, `lyriq`），`6.6.127-android15-8-gb947b5758b2a-ab15580855-4k`。
- 真机日志（`ghostlock-direct-*.log`）：
  - `soc: mtk; kernel_phys_load=0x40080000`（phys 已正确加载）
  - `[-] W1: SELinux: target 0x0000000000000000 is outside the direct map, not attempting`
  - `[-] Write 1 failed`，`native exited code=1`
- 根因：`ResolvedAddresses::data_alias_checked`（`src/core/memory/address_space.cpp:118-124`）用编译期常量
  `kernel::P0_PHYS_OFFSET = target::address::kPhysicalOffset = 0x80000000`
  （`target_constants.hpp:15`）作 DRAM base：
  ```cpp
  const auto physical = kernel_phys_load.checked_add(offset);
  if (!physical || physical->value() < kernel::P0_PHYS_OFFSET) return std::nullopt;
  const uintptr_t direct = (physical->value() - kernel::P0_PHYS_OFFSET) | kernel::P0_PAGE_OFFSET;
  ```
  本机 `kernel_phys_load = 0x40080000 < 0x80000000`，`physical` 恒小于 `P0_PHYS_OFFSET` → 全部判定越界 → `target=0`。
- 该 DRAM base **不是 profile 字段**：native `profile::KernelMisc`（`model.h`）与 Kotlin `NativeProfile` 都没有它，
  profile 无法覆盖。`kernel_phys_load` 与 `P0_PHYS_OFFSET` 是两个不同量：
  - `kernel_phys_load` = 内核镜像 `_text` 物理地址（可配，已对齐）。
  - `P0_PHYS_OFFSET` = DRAM 基址，用于 image→direct-map 换算（不可配，硬编码）。

## 目标与约束

目标：
1. 新增 profile 字段 `kernel_phys_offset`（DRAM base），双侧对齐（Kotlin `NativeProfile` ↔ native `KernelMisc`/`kSections`）。
2. `data_alias_checked` 与相关日志改用该值；未提供时回退编译期 `P0_PHYS_OFFSET`（向后兼容）。
3. extractor/文档/UI 同步：可输出该字段（默认 `null`），编辑页可见，schema 记录语义。

非目标：
- 不改 `kernel_phys_load` 语义、不改 wire 版本、不改 route/攻击阶段。
- 不做“运行时自动推导 DRAM base”（单独议题）。

约束：
- 属**攻击关键路径**（地址解析/资源准备）：需 `cmp_disasm` 8 函数 + 真机门禁 + 归档。
- `P0_PHYS_OFFSET` 仍在 `util.cpp` 日志中出现，需一并改到 `addresses.phys_offset()`，否则日志与行为不一致。

## 改动清单（逐文件）

| 文件 | 改动 | 理由 |
| --- | --- | --- |
| `src/core/profile/model.h` | `KernelMisc` 增 `std::optional<uint64_t> kernel_phys_offset;` | 承载字段 |
| `src/core/profile/binary.cpp` | `kKernel` 增 `OPT("kernel_phys_offset", misc.kernel_phys_offset)` | wire 编解码 |
| `src/core/memory/address_space.h` | `ResolvedAddresses` 增 `uintptr_t phys_offset` + `phys_offset()` 访问器 | 解析结果缓存 |
| `src/core/memory/address_space.cpp` | `init_for_soc` 解析 `kernel_phys_offset`（缺省 `kernel::P0_PHYS_OFFSET`）；`data_alias_checked` 用 `phys_offset` 替换 `P0_PHYS_OFFSET` | 核心修复 |
| `src/core/support/util.cpp` | 日志 `phys_offset=` 与 `delta` 用 `addresses.phys_offset()` | 一致性 |
| `profile-core/.../data/NativeProfile.kt` | `NativeProfileDocument` 增 `kernelPhysOffset: ULong?`；`kernelSection()` 增 key；`from()`/`Builder`/`apply` 增 `kernel_phys_offset` | 双侧契约 |
| `app/.../ui/FieldLabels.kt` | `"kernel_phys_offset" -> R.string.field_kernel_phys_offset` | 标签 |
| `app/src/main/res/values*/strings.xml` | 新增 `field_kernel_phys_offset`（en + zh） | 双语 |
| `app/.../data/AndroidProfileConfigController.kt` | `completeProfileFields` 顶层补 `kernel_phys_offset = null` | 编辑页完整 |
| `tools/extract_rs/src/report.rs` | `render_conf` 输出 `kernel_phys_offset`（默认 `null`）；`ConfInputs` 增字段 | 生成完整骨架 |
| `tools/extract_rs/src/main.rs` | 组装 `ConfInputs.kernel_phys_offset`（默认 `None`） | 同上 |
| `docs/kernel_profiles/PROFILE_SCHEMA.md` / `_ZH.md` | 记录 `kernel_phys_offset` 语义与默认 | 文档 |
| `docs/analysis/device-gates/*.md` | 本机门禁记录 | 归档 |

## 数据流/控制流差异

现状：
```
profile(kernel_phys_load) ─┐
                           ├─> ResolvedAddresses.kernel_phys_load
target const P0_PHYS_OFFSET ─> data_alias_checked  (physical < 0x80000000 → 拒绝)
```

目标：
```
profile(kernel_phys_load)   ─> kernel_phys_load
profile(kernel_phys_offset) ─> phys_offset  (缺省回退 P0_PHYS_OFFSET)
                                │
                                └─> data_alias_checked: physical >= phys_offset 才换算
                                    direct = (physical - phys_offset) | P0_PAGE_OFFSET
```

不变量：
- 未提供 `kernel_phys_offset` 的所有现有设备行为**逐字节不变**（回退 `P0_PHYS_OFFSET`）。
- image→direct-map 的既有先后关系不变；仅把常量替换为可配值。
- 攻击阶段机、waiter/PI 生命周期、payload 布局不变。

## 兼容性与回滚

- 缺省回退 → 现有 profile 与设备零影响。
- wire 为新增 `OPT` key，旧 profile 不出现，解析为空值；版本不变（v2 presence 语义）。
- 回滚：按文件 revert；字段缺失即回退旧常量。

## 验证矩阵

| 批次 | 命令 | 预期 |
| --- | --- | --- |
| native host | `make -C src native-host-tests` | 新增 `address_space` 用例：profile 提供 offset → 正确 direct；缺省 → 0x80000000 路径不变 |
| 编解码一致 | `./gradlew :profile-core:test` / `:app:testDebugUnitTest` | 字段双侧一致；round-trip 含 `kernel_phys_offset` |
| extractor | `(cd tools/extract_rs && cargo test)` | conf 含 `kernel_phys_offset`（默认 null） |
| 反汇编 | `python3 tools/cmp_disasm.py <baseline> build/native/ghostlock` | 8 攻击函数 IDENTICAL 或已复核注解差异 |
| 真机门禁 | 冷机、固定 CPU 对、单 route | W1 不再出现 `target 0x0`；route 命中；归档 `PROFILE-*` |

## 明确保留

- `kernel_phys_load` 语义与推导优先级（profile > SoC 公式）。
- 其余编译期地址常量（`KIMAGE_TEXT_BASE`、`MTK_VADDR_BASE`、`P0_PAGE_OFFSET`、`DIRECT_MAP_*`）。
- route catalog、wire 版本、Kotlin↔native 既有字段名。

## 反汇编核对记录（cmp_disasm）

- 基线：`git stash` 回退本次改动后 `buildGhostlockNative` 产出的二进制；候选：含改动构建。
- 结果（8 攻击函数）：
  - `owner_thread` / `consumer_thread` / `run_main_route_threads` / `do_kernel5_fake_lock_route` / `do_one_write`：IDENTICAL (strict)。
  - `multicast_owner_worker` / `multicast_waiter_worker`：两边都不存在（未实例化）。
  - `waiter_thread`：**DIFF（base=895 / cur=892）**——已复核：
    - 差异仅为 `ResolvedAddresses` 新增成员 `phys_offset` 引起的 `g_exploit_session` 成员偏移位移
      （如 `ldr w0,[x21,#0x190]` → `#0x140`）、随之的 `adrp/add` 数据页基址位移，以及末尾 3 条对齐填充。
    - 将指令中的立即数/绝对地址/符号名归一化后，`waiter_thread` 的助记符与寄存器序列**逐条相同**（仅余末尾 3 行对齐）。
    - 结论：PI/waiter 控制流与步骤顺序未变，属“已复核的注解/布局差异”，不改变等待者生命周期不变量。
- lint：`make -C src lint-tidy` rc=0（0 findings）。

## 进度

- [x] 计划评审确认
- [x] native：字段 + `data_alias` + 日志
- [x] Kotlin：`NativeProfile` + FieldLabels + strings + completeProfileFields
- [x] extractor + docs（54 份 profile 补 `kernel_phys_offset = null`）
- [x] host/round-trip/cargo/gradle/lint 测试
- [x] `cmp_disasm`（见上，waiter_thread 已复核）
- [ ] 真机门禁归档（冷机、固定 CPU 对、单 route，确认 W1 不再 target=0x0）

## 开放决策点

- **D1 字段名**：`kernel_phys_offset`（配 `kernel_phys_load`）是否可接受？或 `kernel_dram_base`？
- **D2 默认值**：缺省回退 `P0_PHYS_OFFSET (0x80000000)`（推荐，零影响），确认？
- **D3 extractor**：是否从 `iomem`/镜像尝试推导并写入？（本计划仅输出 `null`，人工填 0x40000000。）
- **D4 本机取值**：moto MTK6893 用 `0x40000000`（`iomem` DRAM base）确认？
