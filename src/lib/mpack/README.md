# src/lib/mpack —— MessagePack 单文件库（vendored）

> 来源：[ludocode/mpack](https://github.com/ludocode/mpack)，commit `c9d1820`，MIT（见 `LICENSE`）。
> 使用其 **amalgamation**（`tools/amalgamate.sh`）产物：单 `mpack.c` + `mpack.h`。

用途：GLKv3 wire 的 MessagePack 解析（native 侧）。设计见 `docs/analysis/wire-transport-model.md`。

构建注意：
- MPack 是 C99；本仓库项目以 C++ 编译，故需以 **CC** 单独编译 `mpack.c`（Makefile 增加 C 源与规则，或用 `-x c`）。
- 关闭不需要的特性：`MPACK_STDIO`/`MPACK_EXTENSIONS`（以编译定义控制），只保留 reader/expect。
- 第三方告警隔离：单独编译选项或文件名级 flag，**不得**污染项目 `-Wall -Wextra -Wconversion -Wsign-conversion` 零告警。
- 不修改 vendored 源码；上游更新用 commit 记录并重新 amalgamation。
