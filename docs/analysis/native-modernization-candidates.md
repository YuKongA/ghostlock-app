# Native 现代化候选清单

对 `src/core` 的一次排查，目标是列出仍可用 STL / 现代 C++ 收敛的地方，并按
「对内核交互、输出字节、攻击时序」的影响分级。既有设施：`UniqueFd` /
`BorrowedFd` / `ScopeExit` / `Result<T>`（`support/native_resource.hpp`、
`native_result.hpp`）、`target_profile_*` 访问器、`std::span` / `string_view` /
`optional`。

## 约束：native 逐字节 / 指令门禁

本仓库对 native 产物有「逐字节一致 + 设备门禁」要求（CPP14 / CPP17）。直接证据：
`exploit_ops.cpp` 的 `iomem_map_span` 曾把 `getline/free` 换成 `ScopeExit`，因
`run_setup_stage` **多 5 条指令**而在 CPP17 评审被否决、保留 C 写法至今。

所以纯重构虽然逻辑等价，仍会改变指令序列：

- 批次 1 的部分改动（`util.cpp` / `handoff_probe.cpp` / `victim_process.cpp` /
  `route_operations.cpp`）已落地并通过编译 + host，但**合入前需要一次 arm64
  设备门禁回归**；若门禁要求逐字节一致，需按需回退或只保留收益明确的部分。
- `exploit_ops.cpp` 的 `iomem_map_span` / `apply_iomem_cache` **保持现状**（已被
  评审否决过一次，不再尝试）。

## 批次 1 —— 低风险（纯局部，不改内核交互与输出字节）

1. **`FILE*` + `fgets`/`getline` → RAII 读取**
   - `session/handoff_probe.cpp`：`fopen("/proc/modules")`、ksu log 扫描（`FILE*` + `fgets` + `char line[256]`）。
   - `exploit_ops.cpp` `apply_iomem_cache`：`fopen` + `getline` + 手动 `free(line)`。
   - `session/victim_process.cpp`：`fopen("/proc/self/oom_score_adj")`、`/proc/self/comm`。
   - 建议：统一用 `UniqueFd` + 一个 `read_all_fd`/`for_each_line` 小工具（`std::string_view` 行切分），去掉 `FILE*` 与手动 `free`。

2. **定长 `char[]` → `std::array`**
   - `util.cpp`：`char attr[256]`、`enforce[32]`、`status[4096]`、`limits[160]`。
   - `handoff_probe.cpp`：`char line[256]`、`char name[64]`、`resolved[...]`。
   - `victim_process.cpp`：`char comm[24]`、`char script_path[320]`。
   - `routes/route_operations.cpp`：`char sendbuf[64]`、`unsigned char zc[0x40]`。

3. **手写循环 → range-for / `std::find` / `std::any_of`**
   - `handoff_probe.cpp` 的 `strstr` 逐行扫描。
   - `g_*_map` 等 `sizeof(x)/sizeof(x[0])` 遍历 → `std::size` + range-for。

4. **C 数组 → `std::array`**
   - `util.cpp` `fill_profile_cred_copy`：`ref_offsets[]` / `ref_images[]` 局部数组。

5. **`strcmp`/`strncmp` → `std::string_view` 比较**（局部字符串判断）。

6. **手写字节读写 → `std::bit_cast` + `std::span`**
   - `util.cpp` `put32`/`put64`（cred 模板填充）。

## 批次 2 —— 中风险（影响脚本 / 日志字节，需比对输出）

7. `exploit_ops.cpp` `write_root_script`：`char script[12288]` + 单个大 `snprintf`
   拼接 shell 脚本 → `std::string` 逐段拼接。**必须保持脚本文本逐字节一致**。
8. `util.cpp` 日志/属性格式化缓冲（`snprintf` 到定长 buffer）。
9. `profile_binary.cpp` `read_le`/`write_le` → `std::bit_cast` / `std::byteswap`。
   属于 GLK1 布局，需与 v3 字节比对。

## 批次 3 —— 高风险（攻击关键路径 / 布局 / 时序，需设备门禁逐字节）

10. `profile_binary.cpp` `kFields` 的 `offsetof + reinterpret_cast` 字段写入 →
    成员指针 / 类型安全访问。
11. `offsets_json.cpp` `store_profile_scalar` 的 `reinterpret_cast` 写入。
12. `profile_macros.h` / `runtime_struct_offsets.h` 的宏层 → `constexpr` / `inline`
    （LTO 下布局敏感）。
13. `victim_process.cpp` / `route_operations.cpp` 的 fd/pipe 读写时序（**不要动**）。

## 建议推进顺序

- **批次 1**：可先做，编译 + host 测试即可回归；对本机无设备也可推进。
- **批次 2**：改完需 diff 脚本/日志文本。
- **批次 3**：与 step 6 一样，需 arm64 设备门禁；且项目对 native 有「逐字节一致」要求，单独成批。
