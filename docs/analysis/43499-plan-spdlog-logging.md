# 计划：引入 spdlog（异步）作为接缝日志，并暴露给 Lua（2026-10-12 用户裁定）

## 0. 裁定（用户逐字）

> 「你debug这么费劲是因为没有log，引入spdlog，开启异步，所有主要步骤执行前都要输出日志，同时暴露给lua」

⇒ 目的：**主要步骤执行前**即有日志 ⇒ 不再靠"事后从零散 printf 反推"（本会话为此耗了多轮 ✓）。

## 1. 事实（本轮核实 ✓）

```
· spdlog **未 vendored** ✗（`find -iname "*spdlog*"` 空 ✓）；`core/lib/` 现有 `lua` ✓ 与 `mpack` ✓ ⇒ **有现成 vendor 模式** ✓
· 仓内**无**任何日志设施 ✗（`grep spdlog|fmt::` 空 ✓）
· **网络可达** ✓（`curl github.com/gabime/spdlog` ⇒ **200** ✓）⇒ 可获取 ✓
· 现状：全部诊断靠 `std::printf("main: …")` ✗ ⇒ 无级别、无时间戳、无异步、无顺序保证 ✗
```

## 2. 方案（分三步，每步可独立验收 ✓）

```
① **Vendor**：把 spdlog 放进 `core/lib/spdlog` ✓（与 `lua`/`mpack` 同处 ✓）；
   `core/Makefile` 加 `-isystem $(SPDLOG_DIR)/include` ＋ 源（header-only 模式 ✓ 免编译 ✓）。
② **接缝**：新建 `core/src/support/log.hpp` ✓ —— 唯一日志入口（"一处权威" ✓）：
   · `spdlog::init_thread_pool(<队列>, <后端线程>)` ✓ ＋ `async_logger`（**异步** ✓，裁定要求 ✓）
   · 级别与 sink 走 **profile**（✗ 不写死 ✗）：文件 `ghostlock.log` ＋ stderr ✓；
   · 宏/函数名遵循现有风格（Do​xygen 注释 ✓、零告警 ✓、禁 `using` ✓）。
③ **打点**：在**主要步骤执行前**各打一条（⇒ 满足裁定 ✓），至少：
   `route bring_up` ✓／`requeue →` ✓／`park →`（本会话已有临时 printf ✓ ⇒ 迁入 ✓）／`land_word →` ✓／
   `step w1/w2/w3 →` ✓／`prepare →` ✓／`victim spawn →` ✓／`sniff/discovery →` ✓／`access bound →` ✓。
   ⇒ 同时**保留**关键读数的 printf（⇒ 真机判据依赖它们 ✓，先并存后收敛 ✓）。
```

## 3. 暴露给 Lua（按"上层封装用 Lua"裁定 ✓）

```
⇒ 新增一个 **surface 动词**（`contract::script_surface.hpp` 的 `kSurface` 表 ✓）：
    `log(handle, level, message)` ⇒ 落进 `spdlog` ✓（⇒ 脚本可在**每个自选步骤前**打点 ✓）
   · `phase` 取 **`PreWindow`** ✓（⇒ 与 `backend` ✓ 同相；⇒ 不新增内核行为 ✓）
   · `arg_count = 3` ✓（`level` 为 `64` 字节串 ✓、`message` 有界 ✓）；`executable = true` ✓、`recordable = true` ✓
   · 表内注释须同步"由几行变几行"的 pin（⇒ `surface_test.cpp:58` 有该 pin ✗）⇒ 同批更新 ✓
⇒ 并在 `procedures/43499_window.lua` 里于 `surface.step(h, name)` **之前**各打一条 ✓（⇒ 编排在 Lua ✓）。
```

## 4. 验收

```
· 三线门禁：`make -C core -j1 check`（起 109 PASS，只增不减 ✓）／`make -C core -B android`（**零告警** ✓）／`markdownlint` ✓
· **异步**的验证：日志文件里**时间戳单调** ✓ 且**主路径不被写盘阻塞** ✓（⇒ 打点前后 `park/requeue` 的读数不变 ✓）
· 真机：`main: victim uid=0`（本目标 ✓）／日志文件含**步骤前**的每条打点 ✓
· 归档：`docs/analysis/device-gates/*.md`（≤8 KB ✓）
```

## 5. 风险与有序性

```
· 目标为 **aarch64 静态 libc++**（AGENTS §4 ✓）⇒ spdlog 须**header-only**或随静态库 ✓；
· 体量：spdlog 仅头文件即可 ✓（⇒ 不引入编译单元 ✓ 保持构建简单 ✓）；
· 与既有 printf 的**重复**：先并存（⇒ 判据不动 ✓），待日志稳定后**同批清理**临时 printf（§3 有界性 ✓）；
· 本会话**已有的临时探针**（`park about-to` ✓／`waiters-bit` ✓ 等）⇒ 迁入 spdlog 时**一并收敛** ✓。
```
