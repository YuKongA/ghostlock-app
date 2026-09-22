# Native 文件层面重整计划（2026-09-22）

> 目的：先给出全量符号盘点（函数 / 类 / 结构 + namespace），再给出「哪个符号进哪个文件」的目标编排。
> 盘点来源：最终二进制链接符号（`llvm-nm --demangle`，函数/方法）+ 头文件扫描（类型/枚举）；
> 被 LTO 全内联的私有 helper、模板实例与 lambda 不作为独立符号列出。

---

## 1. Namespace 全景

- `ghostlock::kernel` — 内核常量、地址域、`local_sched_attr`、direct-map 末端
- `ghostlock::kernelsnitch` — 上游移植的 KernelSnitch（保持独立，不参与本次重整）
- `ghostlock::profile` — `RouteKind`、`kernel_offsets`、`execution_settings`、`TargetProfile`、layout 结构
- `ghostlock::binary_profile` — GLK1 二进制 profile 编解码
- `ghostlock::legacy` — 旧 JSON profile 转换与 JSON 原语
- `ghostlock::config` — `RuntimeConfig`
- `ghostlock::attack` — profile 安装、地址解析、root 脚本、perf、per-write 原语
- `ghostlock::memory` — 地址解析、堆上下文、payload 构建、`WriteRequest`
- `ghostlock::race` — `PiRace` 与 PI waiter/owner/consumer 线程
- `ghostlock::route`（含 `::tcp_zerocopy` / `::select_stack` / `::multicast_waiter`）— 三路线与分发
- `ghostlock::session` — `ExploitSession`、`ExploitProcedure` 及 W1/W2/W3/handoff
- `ghostlock::session::victim` — victim 进程协议与验证回调
- `ghostlock::support` — RAII 资源、`Result`、通用 helper
- 全局 `main`

---

## 2. 符号清单（按 namespace，不列文件）

### `ghostlock::kernel`

类型/常量：`local_sched_attr`；`PAGE_SIZE`/`PAGE_SHIFT`/`KS_PAGE_SIZE`/`ORDER3_SIZE`/`SKB_*`/`MM_*`/`FAKE_*`/`PSELECT_*`/`DIRECT_MAP_*` 等常量；`KernelAddress` + `Image/Physical/DirectMapAddressDomain`（target.h）；`g_direct_map_end`。

### `ghostlock::kernelsnitch`（移植层，暂不重整）

类型：`kernelsnitch_shared_state`、`KernelSnitchOwner`、`coll_cand_t`、`FutexHashContext`、`inc_arg`、`mm_leak_arg`、`range`。
函数：`context_find_collisions`/`context_destroy`/`__collision_pass`/`__prove_collision_pool`/`__screen_collision_pool`/`__run_mm_leak_pass`/`__mm_leak`/`__increase`/`__do_increase`/`__measure`/`__compare`/`futex_hash_*` 等。

### `ghostlock::profile`

类型：`RouteKind`(enum)、`kernel_offsets`、`execution_settings`、`TargetProfile`、`MulticastWaiterLayout`、`SelectStackLayout`、`TcpZerocopyLayout`。
函数：`route_kind_from_string`、`kernelsnitch_collisions`、`slide_*` 访问器、`task_*`/`cred_*`/`selinux_*`/`empty_zero_page` 访问器、`TargetProfile::*`、`symbol_u32`。

### `ghostlock::binary_profile`

类型/函数：GLK1 头解析与字段 store 表（`kFields` 及字段 lambda 表）。对外仅 `parse(...)`（+ 内部 `write_le`/`read_le`/`store_*`）。

### `ghostlock::legacy`

类型：`ProfileGroups`。
函数：`convert_legacy_offsets`、`fill_external_entry`、`apply_route_branch_values`、`json_*`（`json_skip_ws`/`json_skip_value`/`json_value_span`/`json_object_span`/`json_member_value`/`json_first_member`/`json_parse_int`/`read_namespaced_scalar`）、`g_symbol_map`/`g_task_map`/`g_profile_map`、`load_resolved_profile_json`/`parse_resolved_profile_json`/`read_profile_file`。

### `ghostlock::config`

类型：`RuntimeConfig`。
函数：`RuntimeConfig::init`/`load`/`apply_profile`/`log`、`runtime_config_snapshot`。

### `ghostlock::attack`

函数：`install_profile`、`resolve_profile_addresses`、`log_execution_settings`、`apply_iomem_cache`、`write_root_script`、`perf_find_task`、`slab_drain`、`check_selinux_off`/`enforce_readable`/`process_has_seccomp`（inline）、`in_direct_map`（inline）、`timer_reset`/`timer_ms`/`exploit_t0`、`execution_settings`（inline）。

### `ghostlock::memory`

类型：`ResolvedAddresses`、`SocFamily`(enum)、`HeapContext`、`PayloadPage`、`ReclaimPair`、`MmContextSet`、`PayloadPageState`(enum)、`WriteRequest`、`WriteMode`(enum)、`PayloadWriteLayout`。
函数：`ResolvedAddresses::*`、`HeapContext::init`、`close_ctx_memfds`、`free_ctx_storage`、`encode_compact_waiter`/`encode_multicast_waiter`、`payload_write_layout`、`build_compact_waiter_payload`、`fill_profile_cred_copy`（support 侧 helper）。

### `ghostlock::race`

类型：`PiRace`。
函数：`PiRace::init`/`reset`/`run`/`start_threads`/`request_stop`/`join`、`waiter_thread`、`owner_thread`、`consumer_thread`、`reset_main_route_state`、`run_main_route_threads`、`outcome_with_counters`。

### `ghostlock::route`

类型：`RouteStatus`、`RouteResultCode`(enum)、`RouteController`、`RouteRunResult`、`SelectPolicy`/`TcpPolicy`/`MulticastPolicy`、`RouteLifecycle`(concept)、`TcpZerocopyRoute`、`SelectStackRoute`、`FdSet`、`MulticastWaiterRoute`(resident)。
函数：`run_route`/`run_route_policy`/`run_policy_by_kind`/`route_capability`/`route_needs_ghost_disarm`/`make_route_policy`/`with_route_policy`、`run_route_lifecycle`、`RouteController::init`/`execute`、`do_kernel5_fake_lock_route`/`do_tcp_fake_lock_route`/`do_pselect_fake_lock_route`、`kernel5_resident_start/write/stop`、`resident_route`、`open_selected_fds`、`select_stack_build_fdsets`、`fdset_put_word`/`fdset_get_word`、`reserve_standard_io`、`tcp_punch_thread`、`multicast_owner_worker`/`multicast_waiter_worker`。

### `ghostlock::session`

类型：`ExploitSession`、`ExploitProcedure`、`VictimChain`、`StageResult`(enum)、`VictimRound`(enum)、`HandoffPollPolicy`、`HandoffProbeResult`。
函数：`g_exploit_session`、`make_exploit_procedure`、`ExploitProcedure::{run,setup,w1,w2,w3,handoff,attack_write,retry_write_stage,park_retry_child,resident_write,w1_attempt_cap,w2_fast_repair_prebuild,w2_fast_repair_activate,w1_scratch_repair,w1_resident_repair,w3_exact_target}`、子类 `SelectProcedure`/`TcpProcedure`/`MulticastProcedure`、`handoff_probe_run`、`kernelsu_module_visible`。

### `ghostlock::session::victim`

类型：`VictimContext`、`w2_stage_context`、`w3_stage_context`。
函数：`spawn_victim`、`spawn_child`、`child_main`、`park_child_process_forever`、`verify_selinux_stage`/`verify_w2_stage`/`verify_seccomp_probe_stage`/`verify_leaf_dir_stage`。

### `ghostlock::support`

类型：`UniqueFd`、`BorrowedFd`、`MappedRegion`、`ScopeExit`、`PthreadOwner`、`ChildProcess`、`Result`、`SysError`。
函数：`make_scope_exit`、`read_first_line`、`log_sync`、`log_startup_context`、`disable_rseq_for_thread`、`init_p0_profile`、`futex_op`、`sched_setattr_tid`、`put32`/`put64`、`clone_child`/`clone_leak_child`/`kill_child`、`open_memfd`/`clone_memfd`、`prepare_ctxs`、`prepare_skb_payload`、`prepare_kernel_page`、`prepare_good_kernel_page`、`stash_prebuilt_page`/`activate_prebuilt_page`/`discard_prebuilt_page`/`cleanup_page_prepare_state`、`quarantine/release/close_reclaim_sockets`、`racemask` 相关。

---

## 3. 目标文件编排（哪个符号进哪个文件）

> 原则：一个文件一个「所有者」概念；header 只放对外契约与 inline，`internal`（匿名 namespace）放实现细节；
> `common.h` 拆掉，全局声明回到各自 owner header。

### `core/main.cpp`
`main`（薄适配：解析 argv → 载入 profile → `make_exploit_procedure` → `run`）。

### `core/kernel/`
- `kernel/constants.hpp` ← `PAGE_*`、`KS_*`、`ORDER3_SIZE`、`SKB_*`、`MM_*`、`FAKE_*`、`MS_*`、`PSELECT_*`、`DIRECT_MAP_*`（原 `common.h` 常量）
- `kernel/target.hpp` ← `KernelAddress`、`*AddressDomain`、`STRUCT_PAGE_SIZE`、target 常量（原 `target.h`/`target_constants.hpp`）
- `kernel/direct_map.hpp` ← `g_direct_map_end`（原 `common.h` + `address_space.cpp`）
- `kernel/sched_attr.hpp` ← `local_sched_attr`
- `kernelsnitch/*`（原样保留）

### `core/profile/`
- `profile/model.hpp` ← `RouteKind`、`kernel_offsets`、`execution_settings`、`TargetProfile`、`*Layout`
- `profile/accessors.hpp` ← `route_kind_from_string`、`slide_*`、`task_*`、`cred_*`、`selinux_*`、`empty_zero_page`、`symbol_u32`、`kernelsnitch_collisions`
- `profile/binary.{hpp,cpp}` ← `binary_profile::parse`（原 `profile_binary`）
- `profile/entry.{hpp,cpp}` ← `profile_entry::read_glk1_stdin/file`
- `profile/legacy.{hpp,cpp}` ← `ghostlock::legacy::*`（原 `legacy_support/*`，合并 converter + offsets_json + json 原语）

### `core/attack/`
- `attack/ops.hpp`/`ops.cpp` ← `install_profile`、`resolve_profile_addresses`、`log_execution_settings`、`apply_iomem_cache`、`write_root_script`、`perf_find_task`、`slab_drain`、inline 的 `check_*`/`in_direct_map`/`timer_*`/`execution_settings`

### `core/memory/`
- `memory/address_space.{hpp,cpp}` ← `ResolvedAddresses`、`SocFamily`、`detect_target_soc`
- `memory/heap_context.{hpp,cpp}` ← `HeapContext`、`PayloadPage`、`ReclaimPair`、`MmContextSet`、`PayloadPageState`、`close_ctx_memfds`/`free_ctx_storage`
- `memory/payload_builder.{hpp,cpp}` ← `WriteRequest`、`WriteMode`、`PayloadWriteLayout`、`encode_*`、`payload_write_layout`、`build_*_payload`
- `memory/page_prepare.{hpp,cpp}` ← `prepare_skb_payload`、`prepare_kernel_page`、`prepare_good_kernel_page`、`stash/activate/discard/cleanup_page_prepare_state`、`clone_child`/`open_memfd`/`prepare_ctxs`（原 `util.cpp` 的堆前处理）

### `core/race/`
- `race/pi_race.{hpp,cpp}` ← `PiRace`
- `race/threads.{hpp,cpp}` ← `waiter_thread`/`owner_thread`/`consumer_thread`/`reset_main_route_state`/`run_main_route_threads`（原 `routes/route_threads.*`）

### `core/route/`
- `route/status.hpp` ← `RouteStatus`、`RouteResultCode`
- `route/lifecycle.hpp` ← `RouteLifecycle`、`run_route_lifecycle`
- `route/policy.hpp` ← `RoutePolicy`、`Select/Tcp/MulticastPolicy`、`RoutePolicyList`、`*` 泛型分派函数
- `route/controller.{hpp,cpp}` ← `RouteController`、`RouteRunResult`
- `route/multicast.{hpp,cpp}` ← `MulticastWaiterRoute`、resident wrappers、`do_kernel5_fake_lock_route`
- `route/tcp_zerocopy.{hpp,cpp}` ← `TcpZerocopyRoute`、`tcp_punch_thread`、`do_tcp_fake_lock_route`
- `route/select_stack.{hpp,cpp}` ← `SelectStackRoute`、`FdSet`、`open_selected_fds`、`select_stack_build_fdsets`、`fdset_*`、`do_pselect_fake_lock_route`、`reserve_standard_io`

### `core/session/`
- `session/exploit_session.{hpp,cpp}` ← `ExploitSession`、`g_exploit_session`
- `session/exploit_procedure.{hpp,cpp}` ← `ExploitProcedure` + `Select/Tcp/MulticastProcedure` + `run`
- `session/victim_context.{hpp,cpp}` ← `VictimContext`
- `session/victim_process.{hpp,cpp}` ← `spawn_victim`/`child_main`/`verify_*`（`session::victim`）
- `session/handoff_probe.{hpp,cpp}` ← `HandoffPollPolicy`、`HandoffProbeResult`、`handoff_probe_run`、`kernelsu_module_visible`
- `session/runtime_config.{hpp,cpp}` ← `RuntimeConfig`、`runtime_config_snapshot`
- `session/runtime_paths.hpp` ← 路径常量

### `core/support/`
- `support/resource.{hpp,cpp}` ← `UniqueFd`/`BorrowedFd`/`MappedRegion`/`ScopeExit`/`PthreadOwner`/`ChildProcess`（原 `native_resource`）
- `support/result.hpp` ← `Result`/`SysError`
- `support/proc_io.hpp` ← `read_first_line`、`log_sync`、`log_startup_context`
- `support/thread_helpers.hpp` ← `disable_rseq_for_thread`、`pin_to_core`、`futex_op`、`sched_setattr_tid`
- `support/byte_ops.hpp` ← `put32`/`put64`、reclaim socket 辅助

---

## 4. 迁移顺序与验证

1. 先做无行为风险的物理搬迁（namespace 不变，只改 include）：`route_threads`→`race`、`util` 拆分→`memory/page_prepare`+`support`。
2. 再拆 `common.h`：常量→`kernel/constants.hpp`、全局声明迁回 owner header。
3. 最后拆 route：`do_*` 并入各自 route 文件，`route_operations` 仅留共享 ops。
4. 每步验证：`make ghostlock` + `make native-host-tests` + `make lint-tidy` + `tools/cmp_disasm.py`；攻击路径搬迁按仓库要求走真机门禁。

---

## 5. 执行进度（2026-09-22）

### 已完成（纯文件/头文件搬迁，产物逐字节一致 `0937c8b4…`）
- `routes/` → `route/`
- `routes/route_threads.{hpp,cpp}` → `race/threads.{hpp,cpp}`
- `exploit_ops.{hpp,cpp}` → `attack/ops.{hpp,cpp}`
- `pi_race.{h,cpp}` → `race/pi_race.{h,cpp}`
- `profile_binary.{h,cpp}` → `profile/binary.{h,cpp}`；`profile_entry.{h,cpp}` → `profile/entry.{h,cpp}`
- `legacy_support/` → `legacy/`
- `profile.h` → `profile/model.h`；`profile_macros.h` → `profile/macros.h`
- `target.h`/`target_constants.hpp`/`offset.h`/`runtime_struct_offsets.h` → `kernel/`
- `runtime_time.h` → `support/time.h`；`util.cpp` → `support/util.cpp`
- 拆 `common.h` → `kernel/constants.hpp` + `profile/accessors.hpp` + `support/decls.hpp` + `route/route_api.hpp`（`common.h` 变 umbrella）
- `Makefile`/`CMakeLists.txt`/所有 `#include` 同步

### 待办（会改变 LTO 码型，需专门设备门禁）
- 函数级拆分：`support/util.cpp` 拆 `memory/page_prepare` + `support/*`（与 `ks`/`prepare_ctx` 局部访问宏强耦合）
- `route/route_operations.cpp` 的 `do_*` 并入各 route 文件
- 验证：抽取 `put32`/`put64` 到独立 TU 已使产物由 `0937c8b4` 变为 `43071c99`（LTO 码型变化），已回退；证明函数级拆分非零指令，须成批走门禁

### 第 3 步完成：`route_operations.cpp` 并入各 route 文件（2026-09-22）
- `do_kernel5_fake_lock_route` + `kernel5_resident_*` → `route/multicast_waiter_route.cpp`
- TCP route 级代码 + `tcp_zerocopy` 驱动 → `route/tcp_zerocopy_route.cpp`
- SELECT route 级代码 → `route/select_stack_route.cpp`
- 删除 `route_operations.cpp`（Makefile/CMake 同步）
- **host 测试调整**：`tcp_zerocopy_route.cpp`/`select_stack_route.cpp` 的主机可编译部分（构造/`fail`/`disarm`/`destroy`）保留；route 级代码用 `#if defined(__ANDROID__)` 隔离，主机测试不编译。`multicast_waiter_route.cpp` 不被 host 测试编译，无需隔离。
- `kernel/constants.hpp` 的 `PSELECT_ROUTE_NFDS` 加 `#ifndef` 保护（`select_stack_route.h` 的回退宏在 `common.h` 之前定义会冲突）
- 验证：`make ghostlock` 通过；`make native-host-tests` 全绿；`make lint-tidy` 0。产物 `5da6156a…`（非字节一致，需真机门禁）

## 6. 命名空间 using 规范化（2026-09-22）

规则：只允许 `using namespace ghostlock;`；次级 namespace（`profile`/`memory`/`support`/`route`/`race`/`kernel`/`session`/`config`/`runtime_time`/`legacy`/`kernelsnitch`）必须显式限定。

- 移除 107 条 `using namespace ghostlock::<sub>;`（40 个文件，含 tests）
- 所有裸符号加限定：`profile::execution_settings`/`profile::kernel_offsets`/`memory::WriteRequest`/`memory::WriteMode`/`support::UniqueFd`/`support::MappedRegion`/`support::PthreadOwner`/`support::Result`/`route::RouteStatus`/`route::ROUTE_*`/`config::RuntimeConfig`/`profile::TargetProfile`/`kernel::FAKE_*`/`kernel::P0_*`/`session::g_exploit_session`/`runtime_time::*` 等
- 方法：编译器驱动（移除 using → clang `did you mean` 定位 → 加限定 → 循环）；clang 无建议的（elaborated `struct X`、多义名）手工定向处理
- 验证：`make ghostlock` 产物仍为 `5da6156a…`（**逐字节一致**，零指令）；`make native-host-tests` 全绿；`make lint-tidy` 0

## 7. 可插拔 route 集中 + procedure 分文件（2026-09-22）

- 所有 route 相关文件统一到 `core/route/`；`ExploitProcedure` 基类从 `session/` 移到 `route/exploit_procedure.{hpp,cpp}`
- 三个 procedure 实现各自独立文件（原先三个都在 `exploit_procedure.cpp` 的匿名 namespace）：
  - `route/select_procedure.{hpp,cpp}` — `SelectProcedure`
  - `route/tcp_procedure.{hpp,cpp}` — `TcpProcedure`
  - `route/multicast_procedure.{hpp,cpp}` — `MulticastProcedure`
- `route/exploit_procedure.cpp` 只保留基类实现 + `make_exploit_procedure` 工厂
- 回退上一轮与 route 无关的 `support/native_resource.hpp` 拆分（不在本指令范围）
- 验证：`make ghostlock` 通过；`make native-host-tests` 全绿；`make lint-tidy` 0。产物 `a7c89bcf…`（非字节一致，需真机门禁）

## 8. 固定宽度整型：`int` → `int32_t`（2026-09-22）

- 全 `src/core`（含 tests 与 kernelsnitch）：`int` → `int32_t`，`unsigned int` → `uint32_t`
- 排除字符串字面量与注释；`long int`/`short int` 保留
- 验证：`make ghostlock` 通过；`make native-host-tests` 全绿；`make lint-tidy` 0；无畸形 token、无残留裸 `int`（代码内）
- 产物 `3d65bcc9…`（aarch64 上 `int32_t` 即 `int` 的 typedef，但产物与 `a7c89bcf` 不一致，已确认构建确定性；需真机门禁核对）

## 9. 通用返回状态 `Status`（2026-09-22）

- 新增 `support/status.hpp`：`using Status = bool;`（true=成功），namespace `ghostlock`
- 纯状态返回的 `int` 改为 `Status`：
  - `attack::install_profile` / `attack::resolve_profile_addresses`（原 0/-1）
  - `ExploitProcedure::attack_write` / `retry_write_stage`（原 1/0）
  - `race::run_main_route_threads`（原 1/0）
  - `route::kernel5_resident_start` / `kernel5_resident_write`（原 1/0，wrapper 内 `!= 0`）
  - `support::stash_prebuilt_page` / `activate_prebuilt_page`
  - `MulticastProcedure::resident_write` 的出参 `int32_t &result` → `Status &result`
- 纯布尔返回（`check_selinux_off`/`process_has_seccomp`/`verify_*` 回调）保持原样（回调签名必须为 `int`）
- 验证：`make ghostlock` 通过；`make native-host-tests` 全绿；`make lint-tidy` 0。产物 `23df674d…`（需真机门禁）

## 10. 致命错误 throw + 可继续错误 std::optional（2026-09-22）

- 新增 `support/fatal_error.hpp`：`struct FatalError {}`（throw 点已打印细节，顶层只退出）
- **攻击前致命路径 → throw**：
  - `main` profile 加载失败 → `throw FatalError{}`，`main` 用 `try/catch` 退出 1
  - `attack::install_profile` / `resolve_profile_addresses` → `void` + throw（原 0/-1）
  - `setup` 的 `runtime_config init()` 失败 → throw
  - `make_exploit_procedure` unsupported route → throw
- **可继续/出参 → `std::optional`**：
  - `victim::spawn_victim` → `std::optional<VictimSpawn{pid, task}>`（删除出参）
  - `ExploitProcedure::resident_write` 钩子 → `std::optional<Status>`（nullopt=未处理）
- **PI race / fork / heap spray / W1-W3 重试链保持原样**（log-and-continue / `Status` / 重试），未引入 throw
- 验证：`make ghostlock` 通过；`make native-host-tests` 全绿；`make lint-tidy` 0。产物 `abcaaa1b…`（需真机门禁）

## 11. Profile 配置 getter，消除调用点 cast（2026-09-22）

- `TargetProfile` 新增全部 `execution_settings` 字段的类型化 getter（`w1_attempts()` 返回 `int32_t`、`*_settle_us`/`*_ms`/超时返回 `uint32_t` 等），调用点无需 `static_cast`
- 替换 `attack::execution_settings()->field`（含 `static_cast`）为 `session::g_exploit_session.profile.field()`：`race/threads.cpp`、`route/exploit_procedure.cpp`、`route/multicast_procedure.cpp`、`session/runtime_config.cpp`、`support/util.cpp`
- `TcpZerocopyRoute` / `SelectStackRoute` 改为持有 `const profile::TargetProfile*`（原 `execution_settings*`），用 getter；构造点与 host 测试同步
- 删除已无调用者的 `attack::execution_settings()` 与 tcp/select 内的 `execution_settings()` 静态
- 未做 setter：`TargetProfile` 设计为 immutable，配置写入在 profile 解码层（`offsets_json` 的 `EXEC_FIELD`）
- 验证：`make ghostlock` 通过；`make native-host-tests` 全绿；`make lint-tidy` 0。产物 `c26b0ef2…`（需真机门禁）

## 12. route 配置依赖改引用 / TargetProfile（2026-09-22）

- `TcpZerocopyRoute` / `SelectStackRoute`：成员 `const TargetProfile*` → `const TargetProfile&`（构造成员引用，无空值语义）；构造点传 `session::g_exploit_session.profile`，host 测试传 `profile` 并断言 `&context.profile == &profile`
- `MulticastWaiterRoute`：`execution_settings*` → `const TargetProfile*`（用 getter `profile->multicast_*()`）
  - 仍用指针而非引用：该类的 resident 实例是**文件作用域静态对象**，依赖延迟 `init()` 注入（避免 `g_exploit_session.profile` 的静态初始化顺序问题），引用成员无法延迟绑定
- 删除 `resident_execution_settings()` 等已无调用者 helper
- 验证：`make ghostlock` 通过；`make native-host-tests` 全绿；`make lint-tidy` 0。产物 `a109be11…`（需真机门禁）
