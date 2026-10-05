# A3 kernelsnitch 拆分/改写 计划（2026-10-04）

> 状态：草案，实施中（L 级改动，见 docs/development/engineering-standards.md §1.2）。
> 分支：vr-ko-bypass-dev；基线提交 `8449cd0`（`docs(analysis): A2-4-4 device gate PASS; A2-4 complete`）。
> 权威上游：docs/analysis/top-level-architecture-rewrite-plan.md（Phase A3 / 实施约束 T3）、
> docs/analysis/adr/0001-top-level-architecture.md §3/§17、docs/analysis/adr/0004-framework-convergence.md R1/R13。
> 本文只描述 A3 的设计与批次；所有改动以批次独立门禁推进。

## 0. 结论摘要

- A3 拆两批：**A3-1（本批）** 落 `contract::AddressDiscoveryOps` 抽象 +
  `kernelsnitch` 的**可选适配实现** + 契约测试；**不改动 kernelsnitch 算法、不改动活体调用点**，
  因此泄漏结果与攻击函数机器码都不变。**A3-2（后续，独立门禁）** 才把 provider 物理归位到
  `backend/cve_2026_43499/leak/`，并把 `support/util.cpp` 的花粉喷雾尾部接到 `AddressDiscoveryOps`。
- 关键判据：ADF-0001 §17 明确 `kernelsnitch/`「上游已冻结、可改写」，是 `contract::AddressDiscovery` 的
  **可选实现**，默认随使用的 backend，**有第二个 backend 复用再提升共享**；改写必须在顶级重构拆分之后，
  且不得改变泄漏结果。A3-1 只做接口与适配，不搬目录，正是为满足「第二实现出现前不过早抽象/不过早搬迁」。
- 不变量：泄漏语义与攻击函数行为不变；`cmp_disasm --reviewed` 为**可选诊断**（2026-10-05 起非门槛），判据是真机泄漏门禁；
  泄漏结果与内核版本判定不变。A3-2 才需要重跑真机泄漏门禁。

## 1. 现状与基线

- 基线：分支 `vr-ko-bypass-dev`，提交 `8449cd0`；对照二进制 `build/native/ghostlock-B0`。
- 现有机器码门禁（改动前实测）：
  `python3 tools/cmp_disasm.py --reviewed build/native/ghostlock-B0 build/native/ghostlock`
  → `owner_thread / waiter_thread / consumer_thread / run_main_route_threads /
  do_kernel5_fake_lock_route / do_one_write` 六项 **IDENTICAL (strict)**，`RESULT: PASS`。
- `kernelsnitch/` 现状：上游冻结的 KernelSnitch 移植（碰撞发现 + mm_struct 泄漏扫描）。
  许可/署名分布在 `futex_hash.h`（Bob Jenkins jhash / Jozsef Kadlecsik）与迁移注释中，改写不得删除。
- 另一发现实现：`backend::perf_find_task()`（`backend/cve_2026_43499/primitives.cpp`），
  perf sample 记录投票找当前 task，失败返回 `0`。
- 活体调用点：`support::prepare_kernel_page()`（`support/util.cpp`）内
  `KernelSnitchOwner` → fork 泄漏子进程跑 `context_find_collisions` → 父进程 `scan()` → `result()`。
  该函数不被 6 个 `cmp_disasm` 目标以内联方式调用（目标 `do_one_write` 只引 `prepare_good_kernel_page`）。

## 2. 目标与约束

目标：
1. `contract/` 新增 `AddressDiscoveryOps` 抽象（T3），语义 fail-closed：失败 `ok=false` / 返回 0 时
   不得保留任何部分地址、不得猜测；`kernelsnitch` 与 `perf_find_task` 两种实现同一语义。
2. `kernelsnitch` 作为该抽象的**可选实现**（新子件 `kernelsnitch/address_discovery.h`），保留上游许可/署名。
3. 保留 `kernelsnitch_scan_bounds_test`；新增 `address_discovery_test`（host fake 覆盖 ok / fail-closed）。
4. 更新 include/Makefile/防火墙白名单；stale 清零。

非目标（A3-1 明确不做）：
- 不改 `kernelsnitch.h` 的算法、常量、语句顺序；
- 不搬 `kernelsnitch/` 目录、不改活体喷雾调用点（避免改变泄漏结果与机器码）；
- 不新增参数化 route；不动 wire/Kotlin。

约束（R1）：`contract` 不得 include `backend/platform/terminal/pipeline`。适配头放在 `kernelsnitch/`
（顶级未分层目录，防火墙不追踪）并只 include `contract/address_discovery.hpp` + `kernelsnitch.h`。

## 3. 现状盘点（文件级）

| 文件 | 行数 | 现状 | A3 判定 |
|---|---|---|---|
| src/core/kernelsnitch/kernelsnitch.h | 766 | provider：状态机、碰撞/扫描、`KernelSnitchOwner` RAII、上游许可注释 | **冻结不改**；A3-2 才迁 backend |
| src/core/kernelsnitch/utils.h | 233 | `pr_*` 日志宏 + 平台宏 + 系统头；被 14 个 TU（中性层也有）包含 | 冻结；A3-2 拆分日志到 support |
| src/core/kernelsnitch/timeutils.h | 72 | rdtsc/cntvct 时钟（namespace memory） | 冻结 |
| src/core/kernelsnitch/futex_hash.h | 255 | 纯 jhash/桶算术；host 测试 `futex_hash_test`；含 jhash 署名 | 冻结；host 子件 |
| src/core/kernelsnitch/number_parse.h | 39 | 纯 strtoul 包装；host 测试 `number_parse_test` | 冻结；host 子件 |
| src/core/kernelsnitch/scan_bounds.h | 17 | 溢出安全区间钳制；host 测试 `kernelsnitch_scan_bounds_test` | 冻结；host 子件 |
| src/core/contract/capabilities.hpp | — | `KernelMemoryOps`/`FileCacheWriteOps` 句柄范式 | 范式参照（本批新增同族） |
| src/core/backend/cve_2026_43499/primitives.cpp | — | `perf_find_task()` | 只作语义对照；不新增适配 |

## 4. 目标归属与依赖边

```text
contract/address_discovery.hpp        ghostlock::contract    （新增：结果 + ops 句柄）
kernelsnitch/address_discovery.h      ghostlock::kernelsnitch（新增：可选适配实现）
kernelsnitch/*.h                      ghostlock::kernelsnitch（冻结不动）
A3-2 目标：backend/cve_2026_43499/leak/（provider）；pr_* 日志 → support/（见风险 R-1）
```

依赖边（R1）：`contract/address_discovery.hpp` 只依赖标准库 `<cstdint>/<cstddef>/<concepts>`（不产生层边）；
`kernelsnitch/address_discovery.h` 的 edge 为 `kernelsnitch → contract` 与 `kernelsnitch → kernelsnitch`，
`kernelsnitch` 不在防火墙 8 个受限层内，故不新增白名单、不产生 stale。

## 5. 改动清单（逐文件）

| 文件 | 改动 | 理由 |
|---|---|---|
| `src/core/contract/address_discovery.hpp`（新） | `AddressDiscoveryResult{ok,kaslr_base,init_task,target_task,mm_struct}`；`discovery_failed()`（全零）；`discovery_mm_struct(mm)` / `discovery_target_task(task)`（值为 0 即失败，不猜测）；`fail_closed(result)` 谓词；`AddressDiscovery` 概念（`AddressDiscoveryOps` 满足）；`AddressDiscoveryOps{ctx,discover,available()}` | T3 接口；与 `KernelMemoryOps` 同族（可用性由句柄推导，不设第二 bool 源） |
| `src/core/kernelsnitch/address_discovery.h`（新） | `context_discover(void*,AddressDiscoveryResult*)`：入参为已跑完碰撞阶段的 `KernelSnitchContext`；调用既有 `context_scan`/`context_result`，失败（状态不符 / `~0` 哨兵）写 `discovery_failed()` 并返回 0；成功写 `mm_struct`（含 tag→canonical VA 还原）并返回 1；`address_discovery_ops(KernelSnitchContext*)` 组装句柄 | 把既有 kernelsnitch 暴露为可选 `AddressDiscoveryOps`；不改算法 |
| `src/core/tests/address_discovery_test.cpp`（新） | host fake 覆盖：ok 成功、fail-closed（返回 0 且全零）、`fail_closed()` 对携带残余字段的失败判 false、`available()` 由句柄推导、trivially-copyable/standard-layout | 契约测试；本批验收要求 |
| `src/Makefile` | `NATIVE_HOST_TESTS` 增 `address_discovery_test`；新增其构建规则（仅 `contract/address_discovery.hpp`） | host 门禁接线 |
| `src/core/support/util.cpp` | 增 `#include "kernelsnitch/address_discovery.h"`（仅包含，不调用） | 让适配头进入设备 TU / clang-tidy 覆盖；不改变活体控制流与机器码 |

## 6. 数据流/控制流差异与不变量

- 控制流：A3-1 **零改变**。契约头纯类型；适配头是新增 inline 函数，未被调用，`prepare_kernel_page` 语句与顺序不变。
- 接口形状（新增，T3）：
  ```cpp
  struct AddressDiscoveryResult { bool ok; uintptr_t kaslr_base, init_task, target_task, mm_struct; };
  struct AddressDiscoveryOps { void *ctx; int32_t (*discover)(void *, AddressDiscoveryResult *) noexcept; bool available() const; };
  ```
  `discover` 返回非 0 ⟺ `out.ok == true`；失败必须整体写 `discovery_failed()`（全零），
  绝不保留部分结果——即「fail-closed」。`ok=true` 时值为 0 的字段表示该 provider 未提供，不是猜测。
- 不变量：
  1. 真机泄漏门禁（唯一权威判据）；`cmp_disasm --reviewed` 为可选诊断；
  2. 活体泄漏结果、`last_mm_struct` 语义、内核版本判定不变（真机门禁由主智能体执行）；
  3. `kernelsnitch_scan_bounds_test`（及 `futex_hash_test`/`number_parse_test`）保留通过；
  4. `kernelsnitch.h` 许可/署名逐字保留。

## 7. 分批与门禁

- **A3-1（本批，host-only 交付）**：
  - 门禁：`make -C src native-host-tests`（含防火墙，0 unexpected / 0 stale）；
    `ANDROID_NDK_HOME=... make -C src ghostlock -B` 零告警；
    `ANDROID_NDK_HOME=... make -C src lint-tidy` 0 findings；
    `python3 tools/cmp_disasm.py --reviewed build/native/ghostlock-B0 build/native/ghostlock` 6/6 IDENTICAL。
  - 真机：泄漏结果不变由主智能体按 A3 设备门禁执行（本批不改活体路径，预期与 A2-4-4 一致）。
- **A3-2（后续，独立）**：provider 迁 `backend/cve_2026_43499/leak/`；`utils.h` 日志拆到
  `support/log.hpp` 以断开中性层→backend；喷雾尾部经 `AddressDiscoveryOps` 接入（若为纯重排则需
  证明 sentinel 行为与现流程逐字节一致，否则须完整真机泄漏门禁）。

## 8. 风险与回滚

- R-1（A3-2）：把 `kernelsnitch/` 整体搬 backend 会连累 14 个中性 TU 的 `utils.h` 日志依赖；
  必须先把 `pr_*` 日志拆到 `support`，否则防火墙出现大量 support/session/platform/terminal→backend 违例。
  A3-1 不搬目录即规避。
- R-2：即便「只加一个被 include 的 inline 头」，LTO 也可能因 TU 内容变化改动临近函数机器码。
  以 `cmp_disasm` 实判定；实测（2026-10-04）6/6 strict IDENTICAL，未触发。若将来触发，回退
  `support/util.cpp` 的 include，适配头改为仅由 host 契约测试覆盖（**不可**另起 device TU 只包含
  `kernelsnitch.h`：该头定义非 inline 的 `context_*`，第二个 TU 会重复符号，已实测链接失败）。
- R-3：适配语义若与 `perf_find_task`（失败 0）不一致，契约测试会暴露；两边统一走
  `discovery_*()`（0 值 → 全零失败）。
- 回滚：A3-1 为纯新增 + 一行 include + Makefile 接线；`git checkout -- src/Makefile src/core/support/util.cpp`
  并删除三个新文件即可无损回到 `8449cd0` 行为。

## 9. 明确保留

- PI 窗口规则、四段生命周期、W1/W2/W3 行为与顺序、GLK1 v2 wire、profile 唯一配置权威。
- `kernelsnitch.h` 算法、常量（`FUTEX_SZ`/`IDENTITY_*`/`VA_BITS` 等）与上游署名；`utils.h`/`timeutils.h` 原样。
- 现有防火墙 3 条白名单（均为 `support/util.cpp` 的 43499 依赖，属 A2-5-4 未清，与 A3 无关）。

## 10. 进度

- [x] A3-1 设计文档（本文件）
- [x] A3-1 契约头 + kernelsnitch 适配 + 契约测试 + Makefile
      (`src/core/contract/address_discovery.hpp`、`src/core/kernelsnitch/address_discovery.h`、
      `src/core/tests/address_discovery_test.cpp`；`src/core/support/util.cpp` 仅加一行 include)
- [x] A3-1 门禁：host / NDK 零告警 / lint / cmp 6/6 IDENTICAL
- [ ] A3-1 真机泄漏门禁（主智能体）
- [ ] A3-2 provider 归位 `backend::cve_2026_43499::leak` + `support/log.hpp` 拆分 + 活体接线

## 11. A3-1 门禁记录（2026-10-04）

| 门禁 | 命令 | 结果 |
|---|---|---|
| host | `make -C src native-host-tests` | exit 0；36 项 ok，含 `address_discovery_test`、`kernelsnitch_scan_bounds_test`；防火墙 3 edges / 3 whitelisted / 0 unexpected / 0 stale |
| NDK | `ANDROID_NDK_HOME=... make -C src ghostlock -B` | exit 0，零告警 |
| lint | `ANDROID_NDK_HOME=... make -C src lint-tidy` | exit 0，0 findings（仅非用户头抑制告警） |
| cmp | `python3 tools/cmp_disasm.py --reviewed build/native/ghostlock-B0 build/native/ghostlock` | `RESULT: PASS`，6/6 strict IDENTICAL |
| 真机 | 泄漏结果门禁 | 待主智能体执行（A3-1 未改活体路径） |
