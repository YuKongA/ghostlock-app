[English](kernel-phys-offset-plan.md) | [简体中文](kernel-phys-offset-plan_ZH.md)

# `kernel_phys_offset`（DRAM base）profile 字段计划（2026-10-01）

## 状态与基线

- 分支 `main`，基线 `2d9d801`（含 `02369c1` 的 profile 完整字段；extractor 十进制）。
- 触发设备 MTK6893（moto edge 40，`lyriq`），
  `6.6.127-android15-8-gb947b5758b2a-ab15580855-4k`。
- 真机日志（`ghostlock-direct-*.log`）：
  - `soc: mtk; kernel_phys_load=0x40080000`，phys 已正确加载。
  - `[-] W1: SELinux: target 0x0000000000000000 is outside the direct map, not attempting`
  - `[-] Write 1 failed`，`native exited code=1`。
- 根因：`ResolvedAddresses::data_alias_checked`
  （`src/core/memory/address_space.cpp`）用编译期常量
  `kernel::P0_PHYS_OFFSET = target::address::kPhysicalOffset = 0x80000000`
  （`src/core/kernel/target_constants.hpp:15`）作 DRAM base：

  ```cpp
  const auto physical = kernel_phys_load.checked_add(offset);
  if (!physical || physical->value() < kernel::P0_PHYS_OFFSET) return std::nullopt;
  const uintptr_t direct = (physical->value() - kernel::P0_PHYS_OFFSET) | kernel::P0_PAGE_OFFSET;
  ```

  本机 `kernel_phys_load = 0x40080000 < 0x80000000`，`physical` 恒小于
  `P0_PHYS_OFFSET`，全部判定越界，`target=0`。
- 该 DRAM base 原本不是 profile 字段，native `profile::KernelMisc` 与 Kotlin
  `NativeProfile` 都没有它。
- `kernel_phys_load` 与 `P0_PHYS_OFFSET` 是两个量：前者是内核镜像 `_text` 的物理地址
  （可配、已对齐）；后者是 DRAM 基址，用于 image→direct-map 换算。

## 目标与约束

1. 新增 profile 字段 `kernel_phys_offset`（DRAM base），Kotlin `NativeProfile` ↔
   native `KernelMisc`/`kSections` 对齐。
2. `data_alias_checked` 与相关日志改用该值；未提供时回退编译期 `P0_PHYS_OFFSET`。
3. extractor、文档、UI 同步：可输出该字段，编辑页可见，schema 记录语义。

非目标：

- 不改 `kernel_phys_load` 语义、wire 版本、route/攻击阶段。
- 不在运行时自动推导 DRAM base。

约束：

- 属攻击关键路径（地址解析/资源准备）：需 `cmp_disasm` 8 函数、真机门禁、归档。
- `P0_PHYS_OFFSET` 在 `util.cpp` 日志中也要改为 `addresses.phys_offset()`，否则日志
  与行为不一致。

## 改动（已实施）

- native：`KernelMisc` 增 `std::optional<uint64_t> kernel_phys_offset`；
  `binary.cpp` 增 `OPT("kernel_phys_offset", ...)`；`ResolvedAddresses` 增
  `phys_offset` 与访问器；`init_for_soc` 解析字段，缺省用 `P0_PHYS_OFFSET`；
  `data_alias_checked` 与 `util.cpp` 日志改用该值。
- Kotlin：`NativeProfileDocument` 增 `kernelPhysOffset: ULong?` 与
  `kernel_phys_offset` 键；`FieldLabels`、en/zh `strings.xml`、
  `completeProfileFields` 同步。
- extractor：`render_conf` 输出 `kernel_phys_offset`（默认 `null`），`ConfInputs`
  增字段，`main` 组装默认 `None`。
- 文档：`PROFILE_SCHEMA.md` / `_ZH.md` 记录语义与默认。
- 内置 profile：56 份设备 profile 补 `kernel_phys_offset`（55 份为 `null`，
  lyriq 为 `1073741824`）。

不变量：

- 未提供 `kernel_phys_offset` 的现有设备行为逐字节不变（回退 `P0_PHYS_OFFSET`）。
- image→direct-map 的先后关系不变，只把常量换成可配值。
- 攻击阶段机、waiter/PI 生命周期、payload 布局不变。

## 反汇编核对记录（cmp_disasm）

- 基线：`git stash` 回退本次改动后 `buildGhostlockNative` 产出的二进制；候选：含改动构建。
- 结果（8 攻击函数）：
  - `owner_thread` / `consumer_thread` / `run_main_route_threads` /
    `do_kernel5_fake_lock_route` / `do_one_write`：IDENTICAL (strict)。
  - `multicast_owner_worker` / `multicast_waiter_worker`：两边都不存在（未实例化）。
  - `waiter_thread`：DIFF（base=895 / cur=892），已复核：
    - 差异仅为 `ResolvedAddresses` 新增成员 `phys_offset` 引起的
      `g_exploit_session` 成员偏移位移（如 `ldr w0,[x21,#0x190]` → `#0x140`）、
      随之的 `adrp/add` 数据页基址位移，以及末尾 3 条对齐填充。
    - 归一化立即数、绝对地址、符号名后，助记符与寄存器序列逐条相同（仅余末尾
      3 行对齐）。
    - 结论：PI/waiter 控制流与步骤顺序未变，属已复核的布局差异，不改变等待者
      生命周期不变量。
- lint：`make -C src lint-tidy` rc=0（0 findings）。

## 决策记录

- D1 字段名 `kernel_phys_offset`，与 `kernel_phys_load` 并列（已落地：
  `profile-core/src/main/kotlin/com/ghostlock/app/data/NativeProfile.kt:29`）。
- D2 缺省回退 `P0_PHYS_OFFSET (0x80000000)`（已落地：
  `src/core/memory/address_space.cpp:86-87`）。
- D3 extractor 后续由 `iomem.rs` 从 `/proc/iomem` 推导 DRAM base 与物理加载地址
  （commit `81b25c01`）；没有可用 iomem 时仍输出 `null`。
- D4 moto MTK6893 取 `0x40000000`
  （`app/src/main/assets/kernel_profiles/6.6.127-android15-8-gb947b5758b2a-ab15580855-4k.conf:9`）。

## 验证证据

- [x] `make -C src native-host-tests`：新增 `address_space` 用例，profile 提供
  offset 时得到正确 direct，缺省时 `0x80000000` 路径不变。
- [x] `./gradlew :profile-core:test` / `:app:testDebugUnitTest`：字段双侧一致，
  round-trip 含 `kernel_phys_offset`。
- [x] `(cd tools/extract_rs && cargo test)`：conf 含 `kernel_phys_offset`。
- [x] `cmp_disasm`（见上，`waiter_thread` 已复核）。
- [x] `make -C src lint-tidy` rc=0。

## 剩余工作

- [ ] 真机门禁归档：冷机、固定 CPU 对、单 route，确认 W1 不再出现 `target=0x0`。
  归档位置 `docs/analysis/device-gates/`（当前工作树已删除，历史见
  `git show 064cd1e5^:docs/analysis/device-gates/`）。
