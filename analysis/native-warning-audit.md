# Native 警告策略现状评估（CPP14）

- 方法：NDK `aarch64-linux-android35-clang++` 按生产选项（`-O2 -std=c++20 -fno-rtti`）逐 TU 编译，追加 `-Wall -Wextra -Wconversion -Wsign-conversion`，统计 `warning:` 行（未链接、未改代码）。
- 基线：native `625d5300…`（CPP12m/n 门禁版）。

## 统计结果

| 范围 | 警告数 |
|---|---|
| 生产 TU 合计 | ~60（`main.cpp` 13、`route_operations.cpp` 13、`util.cpp` 23、其余 ≤3/TU） |
| 主机测试 TU 合计 | ~20 |

主要类型（`main/util/route_operations` 抽样）：

| 类型 | 数量 | 说明 |
|---|---|---|
| `-Wsign-conversion` | 18 | 有符号/无符号混合运算与长度/计数字段 |
| `-Wmissing-field-initializers` | 8 | designated init 未列出全部字段（多处为 `RouteStatus status = {.code=...}`） |
| `-Wimplicit-int-float-conversion` | 2 | 时间换算的字面量 |

## 策略建议（不阻塞当前推进）

1. 当前 Makefile 保持 `-Wall -Wno-unused-parameter -Wno-sign-compare -Wno-unused-function`；不在此批次追加 `-Wconversion`（会触发上述约 60 处改动）。
2. 修复应在 **CPP14 收尾**阶段逐文件进行，与最终 `assembleDebug`/门禁同批：
   - `-Wsign-conversion`：优先加显式 `static_cast`/`size_t` 边界，避免改变表达式求值顺序；
   - `-Wmissing-field-initializers`：补齐 designated init 的余下字段（值保持 0）；
   - 不可在不改变已验证布局的前提下修复的项，写入 `-Wno-*` 白名单并给出理由。
3. 测试 TU 的警告随修复一并处理；clang-tidy 采用 `clang-tidy --checks=...` 的显式子集（如 `bugprone-*`、`performance-*` 的关键规则），不做全量格式化。
4. 本评估仅为清单与策略，未修改任何代码或构建选项。

## 修复落地（CPP14，2026-09-18）

- 生产 TU 42 条警告全部修复：`_RSO` 宏链对 `uint64_t` profile 字段与 fallback 使用显式 `(uint32_t)` 截断；`kernelsnitch.h`/`futex_hash.h`/`util.cpp`/`main.cpp`/`route_operations.cpp` 的调用点用显式 cast 对齐目标类型（`pin_to_core(size_t)`、`socklen_t`、`off_t`、`pid_t`、`(int) syscall`、double 换算、数组下标与 `atomic_int` TID）。全部为零指令语义等价替换。
- 二进制验证：`make clean && make ghostlock`（新标志集下）0 警告，native SHA-256 仍为 `625d5300…`；`cmp_disasm.py` 8/8 攻击关键函数 strict 逐指令一致，`full_cmp.py` 全量形状一致。
- 构建策略：Makefile `COMMON_FLAGS` 启用 `-Wall -Wextra -Wconversion -Wsign-conversion`（保留原 `-Wno-unused-parameter -Wno-sign-compare -Wno-unused-function`）；主机测试统一 `HOST_CXXFLAGS` 并让所有测试目标依赖 `$(HDRS)`，头文件变化会触发测试重建。`src/CMakeLists.txt` 同步相同警告集与 `-fno-rtti`。
- 顺带修复两处被缺失头依赖掩盖的主机测试破损：`route_controller_test`（`PiRaceContext` 默认构造，并链接 `native_resource.cpp` 提供 `PthreadOwner` 析构）与 `multicast_waiter_route_test`（CPU 字段显式赋值，同一链接依赖）。`offsets_json_test` 删除未使用的 `total_json`。
- 未做：clang-tidy selected checks 与函数表/UML 最终同步（CPP14 剩余项）。
