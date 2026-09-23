# Native 现代化计划（2026-09-22 第二轮审计）

对 `src/core`（排除 `tests/`、`kernelsnitch/` 上游移植代码）的现代化审计结果与执行计划。
旧版 `native-modernization-candidates.md` 已随分析目录删除；本文件只保留**当前仍未做**的项。

## 范围与约束

- 已有设施：`UniqueFd`/`BorrowedFd`/`ScopeExit`/`Result<T>`、`TargetProfile` 访问器，
  `std::span`/`string_view`/`optional`/`array`；`target.h`、`runtime_struct_offsets.h`、
  `profile_binary` 与 `offsets_json` 的 typed store 均已现代化；`make lint-tidy` 现状 0 findings。
- 攻击关键路径改动按仓库惯例跑 `tools/cmp_disasm.py`（8 个攻击函数）与真机门禁；
  非攻击路径只跑 `make native-host-tests`。
- `kernelsnitch/futex_hash.h` 的 jhash/`OFFSET_OF`/`WAIT()` 等内核原样代码不动。

## 批次 1：低风险（已实施）

- [x] 1. `kernelsnitch/utils.h:32-35` `MAX`/`MIN` 宏经 `common.h` 泄漏到所有 TU，实际调用点仅
  `kernelsnitch/kernelsnitch.h`（6 处）→ `std::min`/`std::max`
- [x] 2. `common.h`：删除重复的 `kernel/runtime_struct_offsets.h` include（第 10/54 行）；
  `#define __ARM 1`（第 7 行）移到 `kernelsnitch/utils.h`（只有 kernelsnitch 的
  `kernelsnitch.h`/`timeutils.h` 使用）
- [x] 3. `attack/ops.hpp:85` `TIMER` 宏 → `attack::timer_mark()` inline 函数（宏内只有
  `log_sync()` 一个理由，函数等价）；替换 `route/exploit_procedure.cpp` 的 9 处调用
- [x] 4. `attack/ops.cpp:27,68` `LOG_EXEC` 函数内 `#define/#undef` → 泛型 lambda（29 处）
- [x] 5. `profile/model.h:114` `uint32_t _pad[3]` 全仓无引用（transport 逐字段解析，不依赖
  `sizeof`/`offsetof`）→ 删除
- [x] 6. 顺手：`tcp_zerocopy_route.h:55`、`select_stack_route.h:83`、
  `multicast_waiter_route.h:41,93` 的 `const struct TargetProfile` → 去掉 `struct`
  （消除 4 处 `-Wmismatched-tags`）

验证：`make -C src native-host-tests` 全绿；`ANDROID_NDK_HOME=… make -C src` 通过，
仅剩保留的 `PAGE_SIZE << mm_slab_order` 一处 `-Wsign-conversion`；`make -C src lint-tidy`
退出码 0、0 findings；`tools/cmp_disasm.py` 对 HEAD 前后二进制 8/8 攻击函数
**IDENTICAL (strict)**。

## 批次 2：攻击路径局部（需 `cmp_disasm.py` + 设备门禁）

- [ ] `support/util.cpp:12-20` 9 个访问宏（`ks`/`mm_objs_per_slab`/`skb_buf`/`prepare_ctx`…）
  → 引用别名（`mm_objs_per_slab` 可写，引用成立）
- [ ] `support/util.cpp:607-626` `pcp_shaping_sv[2]`、`reclaim_sv`、`iovec`/`msghdr` 的 C 数组 +
  `memset` → `std::array` + `{}` 初始化
- [ ] `session/victim_process.cpp:160` `int32_t p1[2], p2[2], p3[2]` → `std::array`
- [ ] `session/victim_process.cpp:121` fd 上限硬编码 1024 → `getrlimit(RLIMIT_NOFILE)`
  （顺带修 >1024 fd 不置 CLOEXEC 的边角）
- [ ] `route/select_stack_route.cpp:181,268` `delays[8]`/`standard_io_backup[3]` →
  `inline constexpr std::array`；`select_stack_route.cpp:190,194,199` C 风格 cast → `static_cast`
- [ ] `memory/heap_context.h:37` `ReclaimPair{int32_t fd[2]}` → `std::array<int32_t,2>`
- [ ] `route/exploit_procedure.cpp:183` `char mod[256]` + `fgets` + `strncasecmp` →
  `std::array` + `string_view::starts_with`（W2 判定路径，此前保留，可再评）
- [ ] `attack/ops.cpp:437-508` `perf_find_task`：`pe{}`、`4096*(1+32)`/`4096*32` 命名常量、
  `__sync_synchronize()` → `std::atomic_thread_fence`、`cands[256]` → `std::array`、
  O(n²) 计票 → `std::count`（算法不变）
- [ ] `kernelsnitch/utils.h:45-56` `SYSCHK` GNU statement expression（45 处）→ 函数模板 + 薄宏
  保留 `#x` 文本；`PAGE_SIZE` 回退宏（唯一 `-Wsign-conversion` 来源）另行评估

## 批次 3：宏层 / 结构（低优先，纯可读性）

- [ ] `legacy/offsets_json.cpp:327,334,411` `SCALAR_STORE`/`SCALAR_ALIAS`/`EXEC_FIELD` →
  C++20 NTTP `template<auto kernel_offsets::*M>`，免宏且强类型（v1 路径）
- [ ] `kernelsnitch/utils.h:73-133` `pr_*` 宏矩阵 → sink 函数 + 薄宏，可补
  `format(printf)` 检查（会改输出路径形状，谨慎）
- [ ] `profile/macros.h` VR 宏：`#ifndef` 默认定义使 `exploit_procedure.cpp:160` 的
  `#ifdef VR_TAG_A_OFF` 恒真，行为需确认后改为显式 feature 开关
- [ ] C++23 评估：`std::expected` 替换手写 `Result<T>` 的 variant 实现、
  `std::to_underlying`、`std::byteswap`（依赖 NDK/libc++ 支持）
- [ ] `runtime_struct_offsets.h:78-136` 访问器样板：评估后保留（每个访问器已是最小形式
  `symbol_u32(&field, fallback)`，改成表驱动反而更难读）

## 明确保留

`futex_hash.h` 内核对移植代码、`fdset_put/get_word` 的 `reinterpret_cast`（POSIX fd_set ABI）、
`put32/put64`（内核布局写入）、`iomem_map_span` 的 getline/free（CPP17 评审否决）、
`PayloadPage` 无析构（内核引用可超生存期）、全局 session 单例（SESSION 设计）、
`binary.cpp` 的 `FIELD` 宏（布局权威）、`profile/model.h:307` `std::strlen`（churn 无收益）。
