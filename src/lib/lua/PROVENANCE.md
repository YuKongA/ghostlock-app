# Lua vendored source — 出处与许可登记

| 项 | 值 |
|---|---|
| 组件 | **Lua 5.5.1**（官方发行版完整源码树） |
| 来源 | `https://www.lua.org/ftp/lua-5.5.1.tar.gz` |
| 发行包 | **398 643 B** · sha256 前 16 = `1c4b4068d67061f2a2231ad2b5422e77` |
| 取回 | 用户在 **2026-10-08** 放入 `src/lib/lua/`，并**确认来源为可信官方源** |
| 旁证（Lead 实测） | 仓内 `src/lib/lua/src` 与官方发行包 **63/63 文件逐字节相同**；无多余件、无缺失件 |
| 许可 | Lua 官方许可（MIT 风格）；文本位置 = `src/lua.h` 的 Copyright 注记 + `doc/readme.html`；`README` 为官方发行说明 |
| 版本裁定 | 用户 2026-10-08 裁定**采用 5.5.1**（ADR-0008 原记 5.4.7；5.4.7 复测值保留作对照） |

## 构建口径（必须遵守）

- **只编译 22 文件核心集**（解析器 + base 库）：
  `lapi lauxlib lbaselib lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio`
- **不进构建**：`lua.c`（解释器）· `luac.c`（编译器/`luac` 工具）· 全部标准库（`io` `os` `package` `debug` `coroutine` `math` `string` `table` `utf8` `db` `loadlib`）
  ⇒ 沙箱在**链接层就没有这些能力**（强于运行期删表）。
- 编译旗标按 `src/lib/mpack` 的同法隔离：C99 + 独立 flag + `-w`（第三方源码不参与本仓告警门槛）。

## 体积（ADR-0008 口径：指令字节 = `llvm-size --format=sysv` 的 `.text*` 段累计；NDK 30 / arm64 / `-Os`）

| 文件集 | Lua 5.5.1（本仓） | Lua 5.4.7（对照） |
|---|---|---|
| 22 文件（解析器 + base 库） | **119 068 B** | 109 620 B（ADR 原文 109 532） |
| 19 文件（去 `llex`/`lparser`/`ldump`） | **95 684 B** | 88 264 B（ADR 原文 88 176） |

⇒ 相对旧实现二进制 4 957 648 B ≈ **2.40%**（5.4.7 为 2.21%）。

## 口径警告（沿用 ADR-0008）

`llvm-size` 的 **Berkeley `text` 列包含 `.rodata` 与 `.eh_frame`**，比指令字节高约 30 KB；**体积比较一律用 `.text*` 累计**。
## 体积实测（可复现 · 2026-10-08 补记）

> 口径 = 指令字节（`llvm-size --format=sysv` 的 `.text*` 段累计）· NDK 30 / arm64 / `-Os`。**数字必须与旗标一起读**。

| 旗标（同一 22 文件集） | 22 文件 | 去解析器 19 文件 |
|---|---|---|
| **纯 `-Os`**（本仓 `make -C src lua-core` 的旗标；**无平台宏**） | **119 000 B** | **95 616 B** |
| `-Os -DLUA_USE_POSIX`（Lead 口径） | **119 068 B** | 95 684 B |
| `-Os -DLUA_USE_LINUX`（探测） | 119 084 B | — |
| `-Oz` / `-fPIC` / `-DNDEBUG` / `-fno-omit-frame-pointer` | 119 000 B（各项同值） | — |
| `-O2`（对照，**不是** `-Os`） | 161 992 B | — |

⇒ **差异定位**：`119068 − 119000 = 68 B` = `-DLUA_USE_POSIX` 平台宏（5.4.7 同现象：ADR 原文 109 532 vs 含该宏复测 109 620，+88 B）。**本仓构建不定义平台宏**（核心集不含 `io`/`os`，无需 POSIX 路径），故**现行口径 = 119 000 B / 95 616 B**。

复现命令：

```sh
make -C src lua-core-size NDK_CC=<aarch64-linux-android35-clang> LLVM_SIZE=<llvm-size>
```
