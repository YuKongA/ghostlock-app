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
   - [x] `util.cpp` `log_startup_context`：`strstr`/`strcspn` 改为 `std::string_view` 游标（`status_view.find`/`find_first_not_of`）
   - [x] 其余遗留 `sizeof(x)/sizeof(x[0])` → `std::size`（`payload_builder.cpp`）
4. C 数组 → `std::array`
   - [x] `util.cpp` `fill_profile_cred_copy` 的 `ref_offsets` / `ref_images`
5. `strcmp`/`strncmp` → `std::string_view` 比较
   - [x] `main.cpp`（argv）、`exploit_ops.cpp`（release/`# ` 头）、`offsets_json.cpp`（`task_`/`cred_`/`off_`/`mcast_` 前缀与 release）、`legacy_profile_converter.cpp`、`address_space.cpp`（`6.12.` 前缀）
     - `exploit_ops.hpp` `process_has_seccomp` 的 `strncmp(line, "Seccomp:", 8)` 随所属定长缓冲一并**保留**（W3 判定相关）
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
- [x] `tools/cmp_disasm.py` 符号拼写刷新：补 `ghostlock::memory::WriteRequest`、`ghostlock::attack::do_one_write`、`ghostlock::route::multicast_waiter::(anonymous namespace)::*` 新拼写（旧版 8 个目标有 5 个 `MISSING`），保留旧拼写向后兼容
- [x] `make lint-tidy` 恢复全绿：`profile.h` move 构造的 `NOLINT(performance-move-constructor-init)` 此前落在诊断行下一行未生效（19 个 TU 均报同一 `-warnings-as-errors`），移到同一行；注释改动零指令，native 仍 `ba395d27…`
- [x] 批次 1 残留 + 定长缓冲 + C 风格 cast 三轮改造完成；`cmp_disasm.py` 8/8 攻击关键函数 strict 逐指令一致、`make native-host-tests` 全绿。cast 批量改造后 native 与改造前**逐字节一致**（`ba395d27…`）。**设备门禁待跑**（strcmp/定长缓冲改动落在 `main` 入口，`main` 指令数 5801→5989；非攻击关键函数）

## 建议推进顺序（剩余）

- [x] 批次 1 残留：`strcmp`/`strncmp` → `string_view`、`util.cpp` 的 `strstr`、其余 `sizeof/sizeof`
- [x] 定长缓冲 → `std::array`/`string_view`：`offsets_json` 的 `route_name[32]` 等、`address_space.cpp value[256]`、`exploit_ops.cpp path[320]`/`stamp[192]`
- [x] C 风格 cast → `static_cast`（算术）/`reinterpret_cast`（指针）（125 行、约 250 处 cast 点，`clang-tidy google-readability-casting --fix`，含 `kernelsnitch/*` 与 `profile.h`；`volatile` 去限定的两处 futex 用 `reinterpret_cast` + `const_cast` 还原原语义；native 产物与改造前**逐字节一致** `ba395d27…`）
- [ ] `util.cpp` 局部访问宏（`ks`/`mm_objs_per_slab`/`skb_buf`/`ctx`…）→ 引用别名（改 attack codegen，需门禁）
- [x] `PAGE_SIZE` / `PSELECT_ROUTE_NFDS` 冗余回退宏：**评审保留**（需 host-safe 独立头 / kernelsnitch 移植攻击路径，见文末「第 5 项回退宏的评审结论」）
  - `kernelsnitch.h:244` / `utils.h:139` 的 `#ifndef PAGE_SIZE #define PAGE_SIZE 4096` 未被 `common.h` 的 `inline constexpr` 拦截，`PAGE_SIZE << mm_slab_order` 仍按 `int` 计算，是当前唯一的 `-Wsign-conversion` 警告来源
- 说明：`perf_find_task` 的 perf ring `reinterpret_cast` 属内核 ABI，建议不动。

## 第三轮排查（2026-09-22，清单之外的新发现）

对 `src/core`（排除 `tests/`、`kernelsnitch/` 移植代码）再做一轮审计，落地：

- [x] 复合字面量 `(T){...}` → `T{...}`：`route_controller.cpp`（5）、`route_threads.cpp`（1）、`pi_race.cpp`（1）
- [x] `offsets_json.cpp` mcast 分支残留的**第四张** `offsetof + reinterpret_cast` 表 → 复用 `scalar_field` 的 `SCALAR_ALIAS` typed store（`waiter_off` 为 `int`，其余 `uint32_t`，由 `decltype` 决定截断，与既有 `SCALAR_STORE` 一致）；host `offsets_json_test` 的 mcast 向量覆盖
- [x] 宏常量 → `inline constexpr`：`PROFILE_JSON_MAX_SIZE`、`TCP_PUNCH_SHMEM_LEN`（后者顺带删除已失效的 `bugprone-implicit-widening` NOLINT，并改 `1ULL << 24` 保型）
- [x] `typedef` → `using`（`exploit_stages.hpp` 函数指针）
- [x] `typedef struct X {…} X;` → `struct X {…};`（`route_controller.h`、`heap_context.h`、`profile.h` 的 layout 结构；`profile.h` 的 tag 由小写重命名为 alias 名，全仓无小写 tag 引用）
- [x] `exploit_ops.hpp` `check_selinux_off` 的 `char b[4]` → `std::array<char, 4>`

验证：`cmp_disasm.py` PASS（8/8 攻击函数**指令形状一致**；6 个函数仅 LTO 标注地址位移，属允许的 `LAYOUT-SHIFT`）；`make native-host-tests` 全绿；`make lint-tidy` 0 findings。native 由 `ba395d27…` 变为 `ca0217aa…`（mcast typed store 改了码型），**需随本批一起走设备门禁**。

## 第四轮：RAII 化（2026-09-22，用户明确豁免「攻击函数指令形状不变」硬约束）

第一版 RAII（三处）曾因 `cmp_disasm.py` 报 **SHAPE-DIFF `run_main_route_threads`** 而回退（native `a79f35d1…` → `ca0217aa…`）。用户指示**不再受该硬约束**后，实施并保留完整 RAII：

- [x] `UniqueFd`：`profile_entry.cpp` `read_glk1_file`、`legacy_profile_converter.cpp` `read_document`、`util.cpp` `/proc/self/status`、`victim_process.cpp` 根脚本 `probe` fd
- [x] `ScopeExit(fclose)`：`exploit_ops.cpp` iomem `FILE*`、`exploit_ops.hpp` `process_has_seccomp`、`exploit_stages.cpp` `/proc/modules`（仅 RAII 化 `fclose`，判定逻辑与 `char` 缓冲不变）

结果：native `cfeafe74…`；`make native-host-tests` 全绿、`make lint-tidy` 0 findings。`cmp_disasm.py` 报 **SHAPE-DIFF `run_main_route_threads`**（`ldr x0, [x8, #0xa70]` → `ldr x0, [x8]`，403 指令数不变，纯 LTO 寻址模式变化；其余 7 个函数为允许的 `LAYOUT-SHIFT`/`IDENTICAL`）。该差异系本次 RAII 触发，已按用户指示**接受**。**真机设备门禁：通过（用户确认，2026-09-22）**。

仍未做（攻击路径，登记）：

- [ ] `util.cpp log_startup_context` 的 `values` 缓冲 → `std::string_view`（诊断日志路径）
- [ ] `route_operations.cpp` 时序阶梯 `delays[]` → `std::array`、`victim_process.cpp` `int p[2]` → `std::array`（攻击路径）
- [ ] `exploit_ops.cpp perf_find_task` 的 O(n²) 计票 → `std::count`、`cands[256]` → `std::array`（攻击关键路径）

### 第 5 项回退宏的评审结论

`select_stack_route.h` 的 `#ifndef PSELECT_ROUTE_NFDS` 与 `kernelsnitch/*` 的 `#ifndef PAGE_SIZE` **评审保留**：

- `select_stack_route.h` 是 host 测试（`tests/select_stack_route_test.cpp`）自包含依赖，不引入 `common.h`（后者拉入 Android/kernelsnitch 平台头）；回退宏是刻意的 host-safe 兜底，删除需先把 `PSELECT_ROUTE_NFDS` 抽到独立的 host-safe 头，属独立重构。
- `kernelsnitch/kernelsnitch.h`/`utils.h` 是上游移植代码且处于攻击路径；`PAGE_SIZE << mm_slab_order` 的 `int` 计算是当前唯一 `-Wsign-conversion` 警告来源，改类型会改变 KernelSnitch 的码型，须走专门门禁。

## 第五轮：route 特判 → `ExploitProcedure` 基类 + Template Method（2026-09-22）

按「基类固定攻击顺序、route 子类 override 特殊攻击逻辑」重构：

- 新增 `session/exploit_procedure.hpp/.cpp`，删除 `session/exploit_stages.hpp/.cpp`：
  - `ExploitProcedure` 基类：`run()` 固定顺序 `setup → W1 → W2/W3 → handoff`；共享方法 `attack_write`（原 `attack::do_one_write` 搬入）、`retry_write_stage`/`root_victim`/`clear_victim_seccomp`/`park_retry_child`；route 步骤为虚钩子（`resident_write`/`w1_attempt_cap`/`w2_fast_repair_prebuild`/`w2_fast_repair_activate`/`w1_scratch_repair`/`w1_resident_repair`/`w3_exact_target`），默认中性
  - 子类 `SelectProcedure`/`TcpProcedure`/`MulticastProcedure`（resident 运行时判定）只 override 各自差异化的攻击逻辑；`make_exploit_procedure(profile)` 单点选择，是唯一的 route 决策；未知/`Auto` route 显式报错返回 `nullptr`（`main` 退出 1），不再静默落到 Select
  - `main.cpp` 改为 `make_exploit_procedure(session)->run(decoded, dump_dir)`；`exploit_ops` 的 `do_one_write` 删除
- **A 选择/执行**：另保留 `routes/route_policy.hpp`（`RoutePolicy` concept + 注册表 + `run_route`/`route_capability` 直接分派）供 PI 路径的 route 原语选择与 `prepare_skb_payload` 的 tcp 几何；`RouteController` 的 `switch(selected)`、`waiter_thread` 嵌套三元、本地 `route::RouteKind` 已删除
- PI 窗口取舍：`run_route`/`route_capability` 用折叠 + `if` 直接分支（不用 `std::visit`）；procedure 的 route 钩子为虚函数，在 PI 窗口内经 vtable 调用（对原「PI 窗口内不得间接分派」约束的显式取舍）

验证：clean build `78e2fd55…`；`make native-host-tests` 全绿；`make lint-tidy` 0 findings。`cmp_disasm.py`（`do_one_write` 目标已重映射到 `ExploitProcedure::attack_write`）：`waiter_thread` 884→906、`attack_write` 406→141（resident 路径移入 `MulticastProcedure::resident_write`）、其余 LAYOUT-SHIFT/IDENTICAL。**攻击函数形状已改变，需真机门禁**。

## 第六轮：W2/W3 拆分 + 指针参数 → 引用（2026-09-22）

- `ExploitProcedure`：删除合并的 `w2_w3()`，拆成 `w2()`（spawn victim + VR tag + cred 写，原 `root_victim`）与 `w3()`（seccomp 清除，原 `clear_victim_seccomp`）；W2/W3 的重试链编排上移到 `run()`
- 指针参数 → 引用（能改的都改）：
  - `victim::spawn_victim(VictimContext&, uintptr_t&)`（原 `*`，内部 `spawn_child`/`child_main` 一并）
  - `victim::w2_stage_context::pipes` / `w3_stage_context::pipes` → `VictimContext&`
  - `ExploitProcedure::attack_write(const WriteRequest&)`、`resident_write(const WriteRequest&, int&)`、`w2(VictimChain&, w2_stage_context&, uintptr_t&)`、`w3(..., w2_stage_context&, uintptr_t)`
  - `support::prepare_good_kernel_page(const WriteRequest&)`、`race::run_main_route_threads(const WriteRequest&)`
  - 保留：`void*` 的 verify 回调 context、内核/`RouteStatus` 的指针 ABI、`RouteController`/`route_policy` 的 `const WriteRequest*`
- `tools/cmp_disasm.py` 候选补 `run_main_route_threads(WriteRequest const&)` 与 `attack_write(WriteRequest const&, char const*)`

验证：clean build `0937c8b4…`；`make native-host-tests` 全绿；`make lint-tidy` 0 findings。`cmp_disasm.py`：`waiter_thread` 884→906、`attack_write` 406→141、其余 LAYOUT-SHIFT/IDENTICAL。**攻击函数形状改变，需真机门禁**。
