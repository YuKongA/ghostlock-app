这是一个超级大PR，基本所有的 native C 代码都重构成更加现代化与易于维护的 C++20 代码。通过零开销抽象、
RAII来实现大规模结构重构， 同时做到在bytecode层面与原有攻击代码大致相同。
并加入了PR [#124](https://github.com/YuKongA/ghostlock-app/pull/124) 的功能。 
同时也已跟上上游进展。由于更改巨大，故版本号前进到1.2。

**需要注意的是，此分支仅测试了5.15 kernel 的 multicast 攻击路径，其他 6.x 攻击路径仍需测试**

This is a massive PR. Almost all native C code has been refactored into more modern and maintainable C++20 code. 
Large-scale structural refactoring was achieved through zero-overhead abstractions and RAII, 
while keeping the bytecode level roughly the same as the original attack code. 
The functionality of PR [#124](https://github.com/YuKongA/ghostlock-app/pull/124) has also been added. It has also caught up with upstream progress.
Due to massive change, the version # forwarded to 1.2.

**Note that this branch has only been tested on the multicast attack path for the 5.15 kernel; the other 6.x attack paths still need to be tested.**

The formal PR note is as follows:

以下为正式 PR note：（下有中文版）

# GhostLock v1.2 Pull Request

Build: Release APK `350` (arm64-v8a, **2.35 MiB**) · exploit native `0109d5a8` (stripped at packaging) · extractor `libextract.so` 2.09 MiB (1.03 MiB in APK)

> For comparison: `libghostlock.so` in APK **95 KiB** (after strip; originally 333 KiB), `classes.dex` 1.15 MiB; Debug APK ~20 MiB.

## Highlights

**New in this branch**

- Native decoupling (S01–S15) + C++20/RAII migration (CPP00–CPP17): C → single-language C++20, reference-alias facade consolidated, resident Multicast refactored into classes, thin `main` adapter
- **Profile editor (advanced section)**: profile source/override status display, editing of 8 execution parameters (W1/W2/W3 attempt counts, W3 chain rounds, route wait, heap prep count, select delay/timeout), one-click apply of recommended core, sparse override save/clear per release
- **Non-core parameters as suggested values**: missing/out-of-range execution parameters fall back to built-in suggestions instead of rejecting the whole profile; Shizuku switch is seeded by profile suggestion, bidirectionally overridden by explicit user choice, and persisted
- **Packaging size reduction**: strip static libc++ debug sections from packaged copy (`libghostlock.so` 1.14 MB → 225 KB), Release APK **2.35 MiB**
- Engineering guardrails: `tools/cmp_disasm.py` disassembly comparison, `make native-host-tests`, clang-tidy and strict warning gates

**Upstream catch-up (U01, item-by-item semantic port)**

- KernelSnitch range-end truncation and direct-map end measurement (`.ghostlock_iomem` cache, `in_direct_map()` out-of-bounds rejection)
- compact value/leaf unified encoding, arm-target consistency validation, W1 `selinux_state.initialized` page-byte filtering
- W3 probe failure retires child; independent KernelSU log path per run (old markers no longer pollute handoff determination)
- 4 retries within the compact Select route (rebuild payload page each time, consumer round increments)
- `SLIDE_*` direct-map task alias (multicast payload consistent with TCP route)
- Google Tensor SoC detection; upstream newly added devices (Pixel 9 Pro / 9 Pro Fold, Honor Magic V5, NX809J/NX888J)
- Remote OTA extraction changed to pure Kotlin (`libextract.so` no longer carries `http-rustls`, 3.60 MB → 2.09 MiB)
- Manual Shizuku switch (from [#59](https://github.com/YuKongA/ghostlock-app/pull/59))

## All code changed, No bytecode changed!
### Truly great zero-overhead abstraction

The native exploit chain was fully rewritten from C to C++20 (RAII / namespaces / layering), while the key exploit instructions retain their instruction-by-instruction shape. Below is a static comparison under the same toolchain and same optimization level:

**Relative to upstream `main` (C single-module + LTO)**

| Metric | Value |
|---|---|
| File size (unstripped) | 93,760 B → 1,140,328 B (+1116%; of which ~800 KB is static libc++ DWARF, ~100 KB symbol table) |
| Packaged (stripped) | 93,760 B → 225,232 B (**+140%**, pure code/data) |
| `.text` | 67,072 B → 213,678 B (+218.6%) |
| Common functions with exactly identical shape | 115/137 (83.9%) |
| **Content change rate** (instruction replacement/add/delete) | 2,614/6,486 = **40.3%** |
| **Order change rate** (instruction position movement) | 873/6,486 = **13.5%** |
| **Total shape change rate** | 3,487/6,486 = **53.8%** (= content + order) |

**Core exploit function breakdown** (the 5 upstream functions with independent symbols available for comparison)

| Function | Instruction count | Content change | Order change | Total shape |
|---|---|---|---|---|
| `waiter_thread` | 1256 → 899 (−28.4%) | 62.0% | 27.8% | 89.8% |
| `do_one_write` | 299 → 186 (−37.8%) | 65.2% | 17.7% | 82.9% |
| `consumer_thread` | 183 → 189 (+3.3%) | 48.6% | 3.8% | 52.5% |
| `owner_thread` | 65 → 74 (+13.8%) | 33.8% | 10.8% | 44.6% |
| `tcp_punch_thread` | 71 → 69 (−2.8%) | 47.9% | 2.8% | 50.7% |

> Methodology: instruction shape = instruction-by-instruction comparison after normalizing all addresses and symbol annotations; sequence differences are decomposed into "content change" (multiset replacement/add/delete) and "order change" (content shared but moved in position), whose sum equals the total shape change rate.
> Core 8 exploit functions = `owner_thread`/`waiter_thread`/`consumer_thread`/`run_main_route_threads`/
> `do_kernel5_fake_lock_route`/`do_one_write`/`multicast_owner_worker`/`multicast_waiter_worker`;
> upstream is an LTO single module; `do_kernel5`/`do_pselect`/`run_main_route_threads`/multicast workers were inlined,
> so only comparable independent symbols are listed.

## Structural changes outside the native part relative to upstream

- `src/kernels/**/offsets.h` (C registry, 48 files) replaced by JSON profiles in `app/src/main/assets/kernel_profiles/`
- Native core unified on C++20 (original `main.c`/`fops.c`/`util.c`/`offsets_json.c` migrated to `.cpp` and layered)
- Profile editor (advanced section) can edit non-core profile parameters, such as retry counts and timeout durations;

## Known limitations

- **TCP Zerocopy / Select Stack** has only fixed host tests; there are no corresponding devices, and real-device gates have not yet been completed
- The URL OTA path has no real-device records yet (the local file path has been in long-term use)

> Data sources and evidence index: `docs/analysis/device-gates/` (Multicast gate chain CPP00–CPP17),
> `docs/analysis/native-cpp-migration-plan.md`, `docs/pr-note-very-not-stable-dev.md`



# GhostLock v1.2 Pull Request

构建：Release APK `350`（arm64-v8a，**2.35 MiB**）· 攻击 native `0109d5a8`（打包时 strip）· 提取器 `libextract.so` 2.09 MiB（APK 内 1.03 MiB）

> 对照：APK 内 `libghostlock.so` **95 KiB**（strip 后，原先 333 KiB）、`classes.dex` 1.15 MiB；Debug APK 约 20 MiB。

## 亮点

**本分支新增**

- Native 解耦（S01–S15）+ C++20/RAII 迁移（CPP00–CPP17）：C → 单语言 C++20，引用别名 façade 收归，
  resident Multicast 类化，`main` 薄适配
- **Profile 编辑器（高级区）**：profile 来源/覆盖状态展示、8 个执行参数编辑（W1/W2/W3 尝试次数、
  W3 链轮数、路由等待、堆准备次数、select 延时/超时）、推荐核心一键应用、按 release 稀疏覆盖保存/清除
- **非核心参数建议值化**：缺失/越界的执行参数回退到内置建议而不是拒绝整个 profile；Shizuku 开关
  由 profile 建议种子、用户显式选择双向覆盖并持久化
- **打包瘦身**：打包副本 strip 静态 libc++ 调试段（`libghostlock.so` 1.14 MB → 225 KB），
  Release APK **2.35 MiB**
- 工程护栏：`tools/cmp_disasm.py` 反汇编对比、`make native-host-tests`、clang-tidy 与严格警告门禁


**上游 catch-up（U01，逐项语义移植）**

- KernelSnitch range-end 截断与 direct-map 末端测量（`.ghostlock_iomem` 缓存、`in_direct_map()` 越界拒绝）
- compact value/leaf 统一编码、arm-target 一致性校验、W1 `selinux_state.initialized` 页面字节过滤
- W3 probe 失败退休 child；每次运行独立 KernelSU 日志路径（旧标记不再污染 handoff 判定）
- compact Select 路线内 4 次重试（每次重建 payload page、consumer 轮次递增）
- `SLIDE_*` direct-map task alias（multicast payload 与 TCP 路线一致）
- Google Tensor SoC 检测；上游新增设备（Pixel 9 Pro / 9 Pro Fold、Honor Magic V5、NX809J/NX888J）
- 远端 OTA 提取改为纯 Kotlin（`libextract.so` 不再携带 `http-rustls`，3.60 MB → 2.09 MiB）
- 手动shizuku开关（来自[#59](https://github.com/YuKongA/ghostlock-app/pull/59)）

## All code changed, No bytecode changed!
### 太伟大了零开销抽象

Native 攻击链从 C 完整重写为 C++20（RAII / 命名空间 / 分层），而攻击关键指令保持逐指令形状不变。以下为同工具链、同优化档下的静态对比：

**相对上游 `main`（C 单模块 + LTO）**

| 指标 | 数值 |
|---|---|
| 文件大小（未 strip） | 93,760 B → 1,140,328 B（+1116%；其中 ~800 KB 为静态 libc++ 的 DWARF、~100 KB 符号表） |
| 打包后（strip） | 93,760 B → 225,232 B（**+140%**，纯代码/数据） |
| `.text` | 67,072 B → 213,678 B（+218.6%） |
| 共同函数形状完全相同 | 115/137（83.9%） |
| **内容变化率**（指令替换/增删） | 2,614/6,486 = **40.3%** |
| **顺序变化率**（指令位置移动） | 873/6,486 = **13.5%** |
| **总形状变化率** | 3,487/6,486 = **53.8%**（= 内容 + 顺序） |

**核心攻击函数分解**（上游有独立符号可对比的 5 个）

| 函数 | 指令数 | 内容变化 | 顺序变化 | 总形状 |
|---|---|---|---|---|
| `waiter_thread` | 1256 → 899（−28.4%） | 62.0% | 27.8% | 89.8% |
| `do_one_write` | 299 → 186（−37.8%） | 65.2% | 17.7% | 82.9% |
| `consumer_thread` | 183 → 189（+3.3%） | 48.6% | 3.8% | 52.5% |
| `owner_thread` | 65 → 74（+13.8%） | 33.8% | 10.8% | 44.6% |
| `tcp_punch_thread` | 71 → 69（−2.8%） | 47.9% | 2.8% | 50.7% |

> 口径：指令形状 = 归一化全部地址与符号注解后的逐指令对比；序列差异分解为「内容变化」
> （多重集替换/增删）与「顺序变化」（内容共有但位置移动），两者之和等于总形状变化率。
> 核心 8 攻击函数 = `owner_thread`/`waiter_thread`/`consumer_thread`/`run_main_route_threads`/
> `do_kernel5_fake_lock_route`/`do_one_write`/`multicast_owner_worker`/`multicast_waiter_worker`；
> 上游为 LTO 单模块，`do_kernel5`/`do_pselect`/`run_main_route_threads`/multicast workers 被内联，
> 故只列可比独立符号。

## 相对上游的native部分以外的结构变化

- `src/kernels/**/offsets.h`（C 注册表，48 个）由 `app/src/main/assets/kernel_profiles/` JSON profile 取代
- Native 核心统一为 C++20（原 `main.c`/`fops.c`/`util.c`/`offsets_json.c` 迁移为 `.cpp` 并分层）
- Profile 编辑器（高级区）可编辑profile的非核心参数，例如重试次数，timeout时间；

## 已知限制

- **TCP Zerocopy / Select Stack** 仅有主机固定测试；无对应设备，尚未完成真机门禁
- URL OTA 路径尚无真机记录（本地文件路径长期使用）

> 数据来源与证据索引：`docs/analysis/device-gates/`（Multicast 门禁链 CPP00–CPP17）、
> `docs/analysis/native-cpp-migration-plan.md`、`docs/pr-note-very-not-stable-dev.md`
