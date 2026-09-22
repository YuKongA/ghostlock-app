# Native 现代化候选清单

对 `src/core` 的排查，列出仍可用 STL / 现代 C++ 收敛的地方，并按「对内核交互、
输出字节、攻击时序」的影响分级。既有设施：`UniqueFd` / `BorrowedFd` / `ScopeExit` /
`Result<T>`、`TargetProfile` 访问器、`std::span` / `string_view` / `optional`。

勾选规则：`[x]` = 已实现，或经评审明确保留（附原因）；`[ ]` = 仍未做，可继续。

## 约束：native 逐字节 / 设备门禁

仓库对布局敏感改动有「逐字节一致 + arm64 设备门禁」要求。证据：`exploit_ops.cpp`
的 `iomem_map_span` 曾把 `getline/free` 换成 `ScopeExit`，因 `run_setup_stage`
多 5 条指令被 CPP17 评审否决，保留 C 写法至今。

## 批次 1 —— 低风险（纯局部，不改内核交互与输出字节）

**进度**：`util.cpp` / `handoff_probe.cpp` / `victim_process.cpp` /
`route_operations.cpp` / `main.cpp` / `legacy_entrypoint_starter.cpp` 的改动已落地并提交。

1. `FILE*` + `fgets`/`getline` → RAII 读取
   - [x] `session/handoff_probe.cpp`：`UniqueFd` + `for_each_line`（`string_view` 行切分）
   - [x] `session/victim_process.cpp`：`FILE*` → `UniqueFd`
   - [x] `exploit_ops.cpp` `iomem_map_span` / `apply_iomem_cache`：**保留**（CPP17 否决，`free(line)` 仍在）
   - [x] `session/exploit_stages.cpp:286` `vr.ko` 检测：**保留**（W3 判定相关）
2. 定长 `char[]` → `std::array`
   - [x] `util.cpp`（`attr`/`enforce`/`status`/`limits`）、`handoff_probe`、`victim_process`、`route_operations`
   - [x] `exploit_ops.hpp` `process_has_seccomp` 的 `char line[256]`：**保留**（W3 判定相关）
3. 手写循环 → range-for / `std::find` / `std::size`
   - [x] `sizeof(a)/sizeof(a[0])` → `std::size`（`route_operations.cpp`、`profile_binary.cpp`、`offsets_json.cpp`，`834280d`）
   - [ ] `util.cpp:90` `strstr(status.data(), names[i])`（`log_startup_context`）
   - [ ] 其余遗留 `sizeof(x)/sizeof(x[0])`
4. C 数组 → `std::array`
   - [x] `util.cpp` `fill_profile_cred_copy` 的 `ref_offsets` / `ref_images`
5. `strcmp`/`strncmp` → `std::string_view` 比较
   - [ ] `main.cpp`（argv）、`exploit_ops.cpp`（release/`# ` 头）、`offsets_json.cpp`（`task_`/`cred_`/`off_`/`mcast_` 前缀与 release）、`legacy_profile_converter.cpp`、`address_space.cpp`（`6.12.` 前缀）
6. 手写字节读写 → `std::bit_cast` + `std::span`
   - [x] `util.cpp` `put32`/`put64`：**保留**（内核布局写入，刻意不动）

## 批次 2 —— 中风险（影响脚本 / 日志字节，需比对输出）

- [x] 7. `write_root_script` `char script[12288]` → `std::string`（脚本文本逐字节一致）
- [x] 8. `util.cpp` 日志/属性格式化缓冲（已随批次 1 完成）
- [x] 9. `read_le`/`write_le` → `std::bit_cast`/`byteswap`：**不采纳**（变长、端序无关，替换更复杂且无收益）

## 批次 3 —— 高风险（攻击关键路径 / 布局 / 时序，需设备门禁）

- [x] 10. `profile_binary.cpp` `kFields` `offsetof + reinterpret_cast` → 类型安全 typed load/store（`8623598`，门禁 `NSMOD-20260922`）
- [x] 11. `offsets_json.cpp` `store_profile_scalar` / 三张 map 的 `reinterpret_cast` 写入 → typed store（`834280d`，门禁 `NSMOD2-20260922`）
- [x] 12. `profile_macros.h` / `runtime_struct_offsets.h` 宏层 → `constexpr`/`inline`（`4e70f2c`）
- [x] 13. `victim_process.cpp` / `route_operations.cpp` fd/pipe 读写时序：**保留**（不要动）

## 本轮额外完成（原清单之外）

- [x] 命名空间收拢：`ghostlock::profile/config/session/route(+tcp/select/mcast)/memory/attack/kernel/support/legacy`；全局与命名空间内兼容别名删除（`ad0873c`…`4878d4f`）
- [x] `common.h` 常量 → `ghostlock::kernel`（`531abf9`）
- [x] `target.h` 88 个宏函数化 → `ghostlock::kernel` `inline constexpr`（`bef7c1c`，门禁 `NSFUNC-20260922`）
- [x] `RouteLifecycle` concept + `run_route_lifecycle`（`8623598`，门禁 `NSMOD-20260922`）
- [x] KernelSU 已加载时跳过攻击的健壮性 guard（`78a4a2a`，真机验证）

## 建议推进顺序（剩余）

- [ ] 批次 1 残留：`strcmp`/`strncmp` → `string_view`、`util.cpp` 的 `strstr`、其余 `sizeof/sizeof`
- [ ] 定长缓冲 → `std::array`/`string_view`：`offsets_json` 的 `route_name[32]` 等、`address_space.cpp value[256]`、`exploit_ops.cpp path[320]`/`stamp[192]`
- [ ] C 风格 cast → `static_cast`（算术）/`reinterpret_cast`（指针）
- [ ] `util.cpp` 局部访问宏（`ks`/`mm_objs_per_slab`/`skb_buf`/`ctx`…）→ 引用别名（改 attack codegen，需门禁）
- [ ] `PAGE_SIZE` / `PSELECT_ROUTE_NFDS` 冗余回退宏（需调整头文件可见性）
- 说明：`perf_find_task` 的 perf ring `reinterpret_cast` 属内核 ABI，建议不动。
