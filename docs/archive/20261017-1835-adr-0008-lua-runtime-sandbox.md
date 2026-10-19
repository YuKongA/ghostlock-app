> 归档说明（2026-10-17）：归档原因 = **ADR 废除**（Lua / script / JCS 载体已删除，见 docs/plan/MASTER-PLAN.md §0.1）。原始路径 = docs/analysis/adr/0008-lua-runtime-sandbox.md；归档路径 = docs/archive/20261017-1835-adr-0008-lua-runtime-sandbox.md。

# ADR-0008：Lua 运行期与沙箱

> **已废除（2026-10-17 用户裁定）**：vendored Lua / mpack / nlohmann 与 GLKv3 文档路径已从构建和源码树删除，
> 本 ADR 的载体（Lua 沙箱、脚本面、文档解析）不再存在；保留此文仅为历史记录，现行边界见 `docs/plan/MASTER-PLAN.md` §0.1。

- 状态：**Proposed**（2026-10-07 提出）——**版本与 vendoring 已裁定（2026-10-08：Lua 5.5.1）；本 ADR 及其 P5 相关三份设计的「批准」仍待用户裁定**
- 索引：[README.md](README.md)
- 日期：2026-10-07（**2026-10-08 改版：引擎版本 → Lua 5.5.1 + 体积口径重测**）
- 相关：ADR-0004（PI 窗口禁间接分派）、ADR-0006（terminal 词汇/归属）、**ADR-0009**（信任模型与硬线）、
  `handoff-payload-plan.md` §10 D22/D26/D30/D31、`handoff-payload-b1b2-runtime.md` §B1.2/§B2.1–B2.5、
  `handoff-payload-kotlin.md` §4/§5、`handoff-plugin-lua.md`

## 背景（Context）

1. **允许导入脚本**（用户裁决 2026-10-07）：profile 可携带 Lua 脚本 ⇒ 需要一个**运行期脚本引擎**，且能力面必须由宿主定义。
2. **PI 窗口约束**（ADR-0004 R16/R21 与 `route_lifecycle.hpp:9-11`）：竞争窗口内**不得有间接分派**；`-fno-rtti` 全局保持。
3. **实测体积（现行 = Lua 5.5.1，本仓口径）**

   - **现行（**本仓口径：纯 `-Os`，无平台宏**；`.text*` 累计 / NDK 30 / arm64）**：**22 文件 = 119 000 B** · **19 文件 = 95 616 B**；相对 4 957 648 B ≈ **2.40%**。
   - **历史对照（5.4.7）**：原文 109 532 / 88 176 B（≈2.2%）· 复测 109 620 / 88 264 B（≤0.1%）。
   - **平台宏变体（对照）**：加 `-DLUA_USE_POSIX` ⇒ 22 = **119 068** B / 19 = **95 684** B（**差 68 B = 平台宏**；5.4.7 同现象 +88 B）。
   - **文件集**：22 = `lapi lauxlib lbaselib lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio`（`.c`）；**19 = 22 − `ldump`/`llex`/`lparser`**。
   - **命令**：源码 = `lua-5.5.1` 官方 tar.gz（**398 643 B** · sha256 前 16 `1c4b4068d67061f2a2231ad2b5422e77`）；`CC = NDK 30` 的 `aarch64-linux-android35-clang`；逐文件 `-c -std=c99 -Os -I. <f>.c`；`llvm-size --format=sysv *.o` 取 **`.text*` 求和**。历史对照 = `lua-5.4.7`（374 097 B · `9fbf5e28ef86c698`）。
   - **⚠ 口径警告**：Berkeley `text` 列含 `.rodata`+`.eh_frame`（**偏高约 30 KB**），**不得用于比较**。
   - **沿革**：曾按错误口径误判原文「不可复现」；经**按段累加**复测纠正 ⇒ 原文成立。
   - **结论**：体积引用取 **119 000 B（≈2.40%，本仓口径）**（平台宏变体 119 068 B 对照；历史 5.4.7 ≈2.2%）；`-fno-unwind-tables` 属可选优化。
4. **来源与许可**：官方 `lua-5.5.1.tar.gz`（**398 643 B** · sha256 前 16 `1c4b4068d67061f2a2231ad2b5422e77`）；**用户 2026-10-08 放入并确认可信**；**旁证 = 63/63 逐字节相同**；**许可** = Lua 官方许可（MIT 风格），文本在 `src/lua.h` + `doc/readme.html`；登记见 [`PROVENANCE.md`](../../src/lib/lua/PROVENANCE.md)（I12）。
5. **选型对照**：QuickJS（367 KiB hello world）· Duktape（160 kB flash/64 kB RAM）· JerryScript（<8 KB RAM）· 自研字节码（最小但需自建编译器）。

## 决策（Decision）

1. **采用 Lua 5.5.1 作为运行期脚本引擎**（2026-10-08 裁定；**替代**原记的 5.4.7，沿革保留），**vendored 到 `src/lib/lua/`**（**已就位**，含 `PROVENANCE.md`）（与 `core/lib/mpack` 同法）；**不引入 `luac`** 跨平台工具链（构建逻辑进 Gradle KTS、不依赖 PATH）。
2. **保留源码解析**（攻击前解析一次）。
   - **修订（2026-10-08）**：执行模型由「两遍」**修订为「单遍 stage」**：脚本窗口外跑一次 ⇒ **staged 计划** ⇒ gate 校验 **staged** ⇒ **按相位执行**；**逐调用边界校验保留**（见 [设计 §3](../analysis/lua-surface-design.md)）。
3. **沙箱白名单**：**不注册** `io`/`os`/`package`/`debug`/`load`/`dofile`/`require`；**脚本可见面 = 7 条**（`backend`/`close`/`writeop`/`copyop`/`settle`/`handoff`/`cfg`；**权威见 [lua-surface-design](../lua-surface-design.md) §1，本 ADR 不重复**）；**沿革**：曾为 `op(token)/probe/handle/slot/require_cap` 形态（已废）。
3.1 **沙箱装配 = 白名单装配（不调用基础库注册器）** —— **5.5 源码实测**：`lbaselib.c:510-532` 的 `base_funcs[]` 注册 **23 个全局**（含 **`dofile`:512 · `loadfile`:516 · `load`:517**，另有 `print`:521（stdout）· `warn`:522 · `getmetatable`:514/`setmetatable`:528 · `collectgarbage`:511）；`linit.c:28-39` 的 `luaL_openlibs` 库表**未编译**（**运行期黑盒**：`load`/`loadfile`/`dofile`/`print`/`io`/`os`/`package`/`debug` 逐一为 **nil**；**`lbaselib` 在编译集内属预期**（它定义 `luaopen_base`，我们刻意保留其函数实现；**禁项表不含它**）；**其余库 opener 定义在未编译的文件里**（如 `luaopen_debug` 在 **`ldblib.c`**、`luaL_openlibs` 在 **`linit.c`**）⇒ `nm` 泛查 `luaopen_*` 会**误报**；**正确判据 = 禁项表（`Makefile:194`，含 `luaL_openlibs`、**不含** `luaopen_base`）逐项 `nm` 全清 + 运行期黑盒 nil**）。⇒ **结论**：**不调用 `luaopen_base` / `luaL_openlibs`**，改由**逐项 `lua_setglobal` 注册白名单** ⇒ `load`/`loadfile`/`dofile` **运行期不存在**（**不是**「注册后删」）。**理由**：「注册后删」= **fail-open**（漏删即开洞）。宿主 API：`lua_newstate`/`lua_setallocf`/`lua_close`/`lua_setglobal`（`lua.h`）· **`lua_sethook`（`lstate.h`）** · `luaL_newstate`（`lauxlib.h`）；**`luaL_openlibs`（`lualib.h`）不调用**。白名单逐项见 [A6 待批单](../../plan/A6-baseline-approval.md) §1 第 2 项。
4. **预算与上限**：`lua_sethook(L, hook, LUA_MASKCOUNT, N)` ⇒ **指令预算** · `lua_setallocf` ⇒ **内存硬上限** · 栈深限制 · **单脚本 ≤64 KB** · 脚本**只在窗口外/操作之间**运行。
5. **wire 承载**：脚本正文 **>256 B**（`kMaxStringBytes`）⇒ **必须走 `bin` 键**，`width` 导出为 `-`；**仅脚本键放开**（其余键仍拒）；同批改 `profile_manifest_v3_test.cpp` 的 `width_column`:165 与 `check_width_matches_wire`:186、重导双副本、重冻 golden。
6. **类型化句柄**（D30）：`{kind, index, generation}` · **有界 64** · 探针产生 · 按 kind 校验 ⇒ **脚本无法伪造地址**。
7. **gate 位置**：`plan_gate` 在 `state_from` 之后、`Backend::run` 之前；**10 条静态原因**（含 `executor-unavailable`）（unknown-op / op-not-available / route-not-available / geometry-missing / capability-missing / order-violation / hash-mismatch / param-invalid / landing-in-pi-window）；`dirty-failure` 属**运行期终止语义**，不进 gate。
8. **`-fno-rtti` 保持**；Lua 只在**窗口外**被调用（绑定层落 `script/`，登记为受限层）。

> **旧树引用说明**：本 ADR 中 `glkv3.hpp:`/`plan_gate`/`state_from`/`Backend::run` 等 `file:line` 属**历史引用**（文件已随 v2 的 `core/src` 重写作废）；语义仍作设计输入，位置以 A1/A6 新树为准。

## 后果（Consequences）

- 二进制增大约 **119 KB（≈2.40%）**（**本仓 119 000 B**；平台宏变体 119 068 B；历史 5.4.7 ≈2.2%）；第三方源码需**登记来源与许可**并隔离告警（I12）—— **已完成**（`src/lib/lua/PROVENANCE.md`）。
- **⚠ 待办（A6 实现批）**：**沙箱参数按 Lua 5.5 API 逐项复核**（`lua_sethook`/`lua_setallocf`/`luaL_*` 装载与栈深；5.5↔5.4 差异逐条核对留证）；复核前本节参数**不视为已验证**。
- 沙箱参数（白名单/预算/内存/64 KB）为**执行期硬约束**，**不得由 profile 放宽**（I13）。
- 新增 `script/` 层与 `op_tu_isolation_test.cpp`/`plan_gate_test.cpp` 承载判据；层表登记同批。
- 失败语义：脚本报错 / 超预算 / 无效句柄 / 越权 API ⇒ **立即终止**（不换路、不回退）。

## 相关（References）

- **ADR-0009**（信任模型与硬线）——本 ADR 只定**运行期与沙箱**；信任与能力面边界见彼。
- `docs/analysis/handoff-payload-plan.md` §10 D22/D26/D27/D29/D30/D31/D32；`handoff-payload-b1b2-runtime.md` §B2。
