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

## 批次 2：攻击路径局部（已实施，待真机门禁）

- [x] `support/util.cpp:12-20` 9 个访问宏（`ks`/`mm_objs_per_slab`/`skb_buf`/`prepare_ctx`…）
  → 匿名 namespace 引用别名；`skb_buf` 因每次 spray 重新分配改为 `skb_buf()` 取值函数
- [x] `support/util.cpp:607-626` `pcp_shaping_sv`/`iovec`/`msghdr` → `std::array` + `{}` 初始化；
  `reclaim_sv` 随 `ReclaimPair` 改 `std::array`（`socketpair` 传 `.data()`）
- [x] `session/victim_process.cpp:160` `int32_t p1[2], p2[2], p3[2]` → `std::array`
- [x] `session/victim_process.cpp:121` fd 上限硬编码 1024 → `getrlimit(RLIMIT_NOFILE)`
  （>1M 或无限时回退 1024；补上 >1024 fd 的 CLOEXEC）
- [x] `route/select_stack_route.cpp:181,268` `delays[8]`/`standard_io_backup[3]` →
  `std::array`（构造参数与成员 `stdio_backup` 同步改 `std::array`）；`:190,194,199,320,375`
  C 风格 cast → `static_cast`
- [x] `memory/heap_context.h:37` `ReclaimPair{int32_t fd[2]}` → `std::array<int32_t,2>`
- [x] `route/exploit_procedure.cpp:183` `char mod[256]` + `strncasecmp` → `std::array` +
  `string_view` 前缀判断（保留大小写不敏感语义）
- [x] `attack/ops.cpp:437-508` `perf_find_task`：`pe{}`、perf ring 尺寸命名常量、
  `std::atomic_thread_fence(acquire)`、`cands` → `std::array`、O(n²) 计票用 `std::count`
- [x] `kernelsnitch/utils.h:45-56` `SYSCHK` statement expression → 立即调用 lambda
  （标准 C++，表达式只求值一次，日志文本不变）；`SYSCHK_pr` 唯一调用点展开为显式 `if`

验证：`make -C src native-host-tests` 全绿；NDK 构建通过（仅剩 PAGE_SIZE 一处
`-Wsign-conversion`）；`make -C src lint-tidy` 退出码 0、0 findings；`tools/cmp_disasm.py`
对批次 1 提交（330df70）构建的二进制 8/8 攻击函数 **IDENTICAL (strict)**（见下）。
**真机设备门禁待跑**（`victim_process.cpp` 的 fd 上限处理、`util.cpp` 引用别名与
`perf_find_task` 均落在攻击路径）。

## 批次 3：宏层 / 结构（已实施）

- [x] `legacy/offsets_json.cpp:327,334,411` `SCALAR_STORE`/`SCALAR_ALIAS`/`EXEC_FIELD`
  → C++20 `template<auto Member>` 成员指针 NTTP（97 处：57 store / 9 alias / 31 exec），
  类型仍由声明成员决定截断（v1 路径，`offsets_json_test` 覆盖）
- [x] `kernelsnitch/kernelsnitch.h:246` `PAGE_SIZE << mm_slab_order` 保持 int 语义但改为
  `static_cast<size_t>(PAGE_SIZE) << ...`：消除全仓最后一个 `-Wsign-conversion`，值不变
- [x] `runtime_struct_offsets.h:78-136` 访问器样板：**评审保留**（每个访问器已是最小形式
  `symbol_u32(&field, fallback)`，表驱动不增可读性）
- [x] `kernelsnitch/utils.h:73-133` `pr_*` 宏矩阵：**评审保留**（上游移植代码 + 输出即门禁
  比对对象，改成函数会改变所有日志路径的形状，收益低于风险）

验证：`make -C src native-host-tests` 全绿；NDK 构建**零警告**；`make -C src lint-tidy`
退出码 0、0 findings；`tools/cmp_disasm.py` 对批次 2 提交（92604fe）构建的二进制
7/8 IDENTICAL (strict)，`do_one_write` 为允许的 LAYOUT-SHIFT（1 个地址注解），PASS。

## 收尾（2026-09-22 第二轮）

- [x] `profile/macros.h` VR 宏整理：追溯 upstream `e9e10f2`（Add vivo t4 support），
  `#ifndef` 默认定义 + `#ifdef` 包裹是移植遗留，恒真即「总是编译」；`VR_TAG_A_OFF` /
  `VR_SYSCALL_TP_FLAG` 未被代码引用（tag A 随 `thread_info.flags` 整字清零）。
  处理：去掉死条件，VR 块改为无条件编译（运行时 `/proc/modules` 检测决定是否写），
  `VR_TAG_B_OFF` 保留为可 `-D` 覆盖的宏，`macros.h` 精简为纯宏头。
  验证：8/8 攻击函数 IDENTICAL (strict)，host/lint/构建全绿——行为与码型零变化。
- [x] C++23 评估与实施：NDK 30（clang 21 + libc++）与 host Apple clang 21 均支持
  `std::expected` / `std::to_underlying` / `std::byteswap`（探测编译通过）。
  已把 `Makefile`（`CXXFLAGS` / `HOST_CXXFLAGS` / `lint-tidy`）与 `CMakeLists.txt`
  升到 `c++23`。验证：host tests 全绿、NDK 构建零警告、`lint-tidy` 0 findings、
  8/8 攻击函数对 C++20 构建 IDENTICAL (strict)——标准升级零码型变化。
  下一步（独立批次）：`std::expected` 替换手写 `Result<T>`
  （`support/native_result.hpp`）。

## 明确保留

`futex_hash.h` 内核对移植代码、`fdset_put/get_word` 的 `reinterpret_cast`（POSIX fd_set ABI）、
`put32/put64`（内核布局写入）、`iomem_map_span` 的 getline/free（CPP17 评审否决）、
`PayloadPage` 无析构（内核引用可超生存期）、全局 session 单例（SESSION 设计）、
`binary.cpp` 的 `FIELD` 宏（布局权威）、`profile/model.h:307` `std::strlen`（churn 无收益）。
