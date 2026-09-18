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
