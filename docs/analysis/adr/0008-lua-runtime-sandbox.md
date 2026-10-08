# ADR-0008：Lua 运行期与沙箱

- 状态：**Proposed**（2026-10-07 提出）——**版本与 vendoring 已裁定（2026-10-08：Lua 5.5.1）；本 ADR 及其 P5 相关三份设计的「批准」仍待用户裁定**
- 索引：[README.md](README.md)
- 日期：2026-10-07（**2026-10-08 改版：引擎版本 → Lua 5.5.1 + 体积口径重测**）
- 相关：ADR-0004（PI 窗口禁间接分派）、ADR-0006（terminal 词汇/归属）、**ADR-0009**（信任模型与硬线）、
  `handoff-payload-plan.md` §10 D22/D26/D30/D31、`handoff-payload-b1b2-runtime.md` §B1.2/§B2.1–B2.5、
  `handoff-payload-kotlin.md` §4/§5、`handoff-plugin-lua.md`

## 背景（Context）

1. **允许导入脚本**（用户裁决 2026-10-07）：profile 可携带 Lua 脚本 ⇒ 需要一个**运行期脚本引擎**，且能力面必须由宿主定义。
2. **PI 窗口约束**（ADR-0004 R16/R21 与 `route_lifecycle.hpp:9-11`）：竞争窗口内**不得有间接分派**；`-fno-rtti` 全局保持。
3. **实测体积（**现行口径 = Lua 5.5.1**，2026-10-08）**

   - **现行（Lua 5.5.1 实测，同法：`.text*` 累计 / NDK 30 / arm64 / `-Os`）**：**22 文件 = 119 068 B**；**19 文件（去解析器）= 95 684 B**；参照二进制 **4 957 648 B** ⇒ 核心 ≈ **2.40%**。
   - **历史对照（Lua 5.4.7；保留，不删）**：**原文** = `109 532 B ≈ 107 KB` / 去解析器 `88 176 B ≈ 86 KB`（⇒ 当时口径 ≈2.2%）；**复测** = **109 620 B** / **88 264 B**（与原文相差 ≤0.1%，已证原文成立）。
   - **归档者独立复测（5.5.1）**：22 = **119 000 B** / 19 = **95 616 B**（与方法差异 ≤68 B ≈ 0.06% ⇒ 与 119 068 / 95 684 互为旁证）。
   - **文件集（22 / 19）**：22 = `lapi lauxlib lbaselib lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio`（`.c`）；**19 = 22 − `ldump`/`llex`/`lparser`**。
   - **命令（可复核，现行）**：源码 = `lua-5.5.1` 官方 tar.gz（**398 643 B**，sha256 前 16 = **`1c4b4068d67061f2a2231ad2b5422e77`**）；`CC = NDK 30.0.16248370` 的 `aarch64-linux-android35-clang`；逐文件 `$CC -c -std=c99 -Os -I. <f>.c`；**按段累加**：`$NDK/…/llvm-size --format=sysv *.o` 取 **`.text*` 各行求和**（`.text` 与各 `.text.<symbol>`）。**历史对照命令**：源码换 `lua-5.4.7`（374 097 B，sha256 前 16 = `9fbf5e28ef86c698`），其余同法。
   - **⚠ 口径警告（不得用于比较）**：`llvm-size` **Berkeley 格式的 `text` 列** = `.text*` **+ `.rodata` + `.eh_frame`**（含只读数据与展开表），用它求和会**偏高约 30 KB**（本例 22 文件：140 036 vs **109 620**）。算例（`lvm.o`）：**13 704（`.text*`）+ 222（`.rodata.str1.1`）+ 1 000（`.eh_frame`）= 14 926** = 其 Berkeley `text` 列。⇒ **体积比较一律用 `.text*` 累加**。
   - **沿革**：2026-10-08 曾以**错误口径**（Berkeley `text` 列）复测并据此误判原文数字「不可复现」（当时读数 140 036 / 115 476 等）；经**决定性复测**（按段累加）纠正 —— **原文数字成立**，本节结论以本口径为准。
   - **结论（**采用决定不变**）**：体积引用取 **109 532 / 109 620 B**（≈ **107 KB**，≈ 现役二进制的 **2.2%**）；加 `-fno-unwind-tables` 一类进一步缩小属可选优化。
4. **来源与许可（vendoring 登记，2026-10-08）**：**官方 `lua-5.5.1.tar.gz`**（**398 643 B**，sha256 前 16 = **`1c4b4068d67061f2a2231ad2b5422e77`**）；**用户 2026-10-08 放入 `src/lib/lua/` 并确认来源为可信官方源**；**旁证 = 仓内 `src/lib/lua/src` 与官方发行包 63/63 文件逐字节相同**（无多余/无缺失）；**许可** = Lua 官方许可（MIT 风格），文本位置 = `src/lua.h` 的 Copyright 注记 + `doc/readme.html`；**完整登记见 [`src/lib/lua/PROVENANCE.md`](../../src/lib/lua/PROVENANCE.md)**（I12 要求）。
5. **选型对照**：QuickJS（官方 367 KiB x86 hello world）、Duktape（160 kB flash / 64 kB RAM）、JerryScript（<8 KB RAM）、自研字节码（最小但要自建编译器）。

## 决策（Decision）

1. **采用 Lua 5.5.1 作为运行期脚本引擎**（2026-10-08 裁定；**替代**原记的 5.4.7，沿革保留），**vendored 到 `src/lib/lua/`**（**已就位**，含 `PROVENANCE.md`）（与 `src/lib/mpack` 同法：C99、独立 flag、`-w`；参照 `src/Makefile:94-105`）；**不引入 `luac`** 跨平台工具链（构建逻辑进 Gradle KTS、不依赖 PATH）。
2. **保留源码解析**（脚本只在攻击前解析一次）。
   - **修订（2026-10-08 裁定取代）**：执行模型由「两遍（录制→执行）」**修订为「单遍 stage」** —— 脚本窗口外跑一次 ⇒ **staged 计划** ⇒ `plan_gate` 校验 **staged 产物** ⇒ **按相位执行**；**逐调用边界校验保留**（见 [lua-surface-design](../analysis/lua-surface-design.md) §3）。
3. **沙箱白名单**：**不注册** `io` / `os` / `package` / `debug` / `load` / `dofile` / `require`；只暴露 `op(token, table)` / `probe(name)` / `handle(kind)` / `slot.get|set` / `require_cap(name)`；`math` 只留整数。
4. **预算与上限**：`lua_sethook(L, hook, LUA_MASKCOUNT, N)` ⇒ **指令预算**；`lua_setallocf` ⇒ **内存硬上限**；栈深度限制；**单脚本 ≤64 KB**；脚本**只在操作之间**运行（PI 窗口内禁调用）。
5. **wire 承载**：脚本正文 **>256 B**（`kMaxStringBytes`，`glkv3.hpp:84`）⇒ **必须走 `bin` 键**，`width` 导出为 `-`，`kMaxBinBytes` 落 `glkv3.hpp`；**仅脚本键放开**（其余键仍拒：`glkv3_parse.cpp:631-633`）；同批改 `profile_manifest_v3_test.cpp` 的 `width_column`:165 与 `check_width_matches_wire`:186、重导双副本、重冻 golden。
6. **类型化句柄**（D30）：`{kind, index, generation}`，**有界 64**，由探针产生，使用时按 kind 校验 ⇒ **脚本无法伪造地址**。
7. **gate 位置与原因**：`plan_gate` 在 `state_from` 成功之后、`Backend::run` 之前；**10 条静态原因**（含 `executor-unavailable`）（unknown-op / op-not-available / route-not-available / geometry-missing / capability-missing / order-violation / hash-mismatch / param-invalid / seam-in-pi-window）；`dirty-failure` 属**运行期终止语义**，不进 gate。
8. **`-fno-rtti` 保持**；Lua 只在**窗口外**被调用（绑定层落 `script/`，登记为受限层）。

> **旧树引用说明**：本 ADR 中出现的 `glkv3.hpp:`、`glkv3_parse.cpp:`、`plan_gate`、`state_from`、`Backend::run` 等 `file:line` 属**历史引用**（其文件已随 [rewrite-plan-v2](../../plan/rewrite-plan-v2.md) 的 `src/core` 全量重写作废）；命名与语义仍作设计输入，位置以 v2 的 A1/A6 新树为准。

## 后果（Consequences）

- 二进制增大约 **119 KB（≈2.40%）**（5.5.1 现行口径；历史 5.4.7 ≈2.2% 作对照）；第三方源码需**登记来源与许可**并隔离告警（I12）—— **已完成**（`src/lib/lua/PROVENANCE.md`）。
- **⚠ 待办（A6 实现批）**：**沙箱参数须按 Lua 5.5 的 API 逐项复核**（`lua_sethook` / `lua_setallocf` / `luaL_*` 装载与栈深限制等；5.5 与 5.4 的 API/默认行为差异须逐条核对并留证），复核前不得把本节参数视为已验证。
- 沙箱参数（白名单/预算/内存/64 KB/操作间执行）为**执行期硬约束**，**不得由 profile 放宽**（I13）。
- 新增 `script/` 层与 `tests/op_tu_isolation_test.cpp`、`tests/plan_gate_test.cpp` 承载判据；层表登记同批（见 `handoff-payload-b0-contract.md` §4）。
- 失败语义：脚本报错 / 超预算 / 无效句柄 / 越权 API ⇒ **立即终止**（脏状态语义、不换路、不回退）。

## 相关（References）

- **ADR-0009**（信任模型与硬线）——本 ADR 只定**运行期与沙箱**；信任与能力面边界见彼。
- `docs/analysis/handoff-payload-plan.md` §10 D22/D26/D27/D29/D30/D31/D32；`handoff-payload-b1b2-runtime.md` §B2。
