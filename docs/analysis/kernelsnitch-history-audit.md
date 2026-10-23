# KernelSnitch 历史提交与归档审计（2026-10-21）

## 1. 审计范围

- 全仓所有 ref：9 个 refs（local + remotes）。
- 提交总数：**2294**。
- 时间范围：`2026-07-29`（Initial commit）到 `2026-10-21`（2.0.5）。
- 方法：`git log --all` 全量扫描 subject/body；对 `kernelsnitch|snitch|futex|mm_struct|leak` 命中项
  逐个 `git show` 阅读提交说明与补丁；对删除路径做 `--diff-filter=D` 追踪。
- 只读操作，未改工作树。

## 2. 核心发现

**这次 leak 的根因修复早已存在**：

```text
aaaa6503d791e6ee24845856a09c3e7fe15e74d0
2026-09-20
fix(native): size the futex table from possible CPUs instead of online, like the kernel
```

补丁内容：

- 新增 `futex_hash_table_size_for()`：`roundup_pow_of_two(256 * possible)`；
- `kernelsnitch_context_init` 从 `_SC_NPROCESSORS_CONF` 取 possible，而不是 online；
- 表值从 `256*online*2`（A301SO 为 4096）改为 **2048**；
- host 测试锁定 `futex_hash_table_size_for(8)==2048`。

该提交在 `dev-nocpp` 的祖先中，但它修的是 C++ 路径
`src/core/kernelsnitch/kernelsnitch.h`；当前 C 副本
`app/src/main/cpp/glkcommon/kernelsnitch/kernelsnitch.h` 没有同步这条修复，所以冷机
仍以 4096 分桶匹配内核 2048，leak 恒 `-1`。

本次已把该修复回移到 C 副本，并补 host 2048/4096 对比测试。

## 3. 历史修复提交

| 日期 | 提交 | 主题 | 与 leak 的关系 |
|---|---|---|---|
| 2026-09-09 | `35b98540` | fix kernelsnitch leak and collision handling (#115) | 候选校验、mm canonical/range、tag sweep |
| 2026-09-10 | `73efd632` | retry tagged mm candidates after canonical miss (#121) | canonical 失败后扫 tag |
| 2026-09-13 | `163c6a31` / `0b12c21` | add explicit hash context | 把表尺寸/桶计算显式化 |
| 2026-09-13 | `c29452a5` / `c3ef036` | owned context lifecycle | 上下文生命周期 |
| 2026-09-15 | `396e52d` | cache direct map end from /proc/iomem (#138) | 扫描上限/越界崩溃 |
| 2026-09-17 | `f8a50857` / `cb35c9d9` | lock futex hash vectors and fix state labels | 状态表缺 `collisions not found` |
| 2026-09-17 | `35b850b8` / `d2a142b5` | cache direct map end and bound leak scan | slice/identity_diff 收窄 |
| 2026-09-20 | **`aaaa6503`** | **size futex table from possible CPUs** | **本次 leak 根因修复** |
| 2026-09-22 | `5dd1a3f9` | keep KernelSnitch slab shift in size_t | `PAGE_SIZE << order` 符号转换 |
| 2026-09-22 | `994ccf81` | close the leak memfd when reaping the snitch child | 修 `SYSCHK_pr` 展开丢 close |
| 2026-10-02 | `841e7231` | squash (27 commits) | 路径重组 |
| 2026-10-04 | `d3de8764` | A3-2 move provider to 43499 leak | `src/core/kernelsnitch/*` 移到 backend |
| 2026-10-08 | `b0e9f499` | clear src/core for full rewrite | 删除旧 C++ core |
| 2026-10-08 | `a0d6610f` | bring futex bucket hash back, fixed vectors | C++ 版固定向量 |
| 2026-10-08 | `7a40bfa5` | derive collision search shape | table/possible CPU 语义 |
| 2026-10-08 | `48968b3f` | the collision rule | 真实桶规则 |
| 2026-10-08 | `9ae06a43` | two halves agree on one address | search↔prepare 接合 |
| 2026-10-08 | `c46099e6` | the mm is what search finds | mm 不是输入 |
| 2026-10-08 | `1ba35d29` | preparation runs twelve steps | 表预算 16384→2048 |
| 2026-10-08 | `56ed2f9c` | wake protocol | 唤醒/放弃清理 |
| 2026-10-15 | `dfbb942a` | R2 naming, landing primitives lose 'seam' | 删除 `snitch_seam_test.cpp` |
| 2026-10-18 | `2933b838` | 2.0 | 删除 `core/.../leak/snitch_*`、owner、probes、profile |
| 2026-10-21 | `d2778a1a` | 2.0.5 | 删除剩余 `core/.../leak/snitch_*` 与 tests |

补充：`217bbb04`（fix(leak): one object is an answer）、`0252e51d`（two refusals stop
sharing one name）、`4c134d9f`（two phases of a leak search）也属于 leak 语义修复，
但未在本次冷机路径中直接触发。

## 4. 已归档记录

| 当前路径 | 原路径/来源 | 状态 | 内容 |
|---|---|---|---|
| `docs/archive/20261007-2237-a3-kernelsnitch-plan.md` | `docs/plan/a3-kernelsnitch-plan.md` | 归档（被 MASTER-PLAN 取代） | A3 拆分/适配计划；含 `kernelsnitch.h` 766 行、scan_bounds、futex_hash、A3-2 迁 backend |
| `docs/archive/20260922-2231-upstream-catch-up-20260913.md` | `docs/analysis/upstream-catch-up-20260913.md` | 删除后恢复归档 | `396e52d` direct-map 末端、range-end 截断、`g_direct_map_end` |
| `docs/archive/20260922-2231-native-modernization-plan.md` | `docs/development/native-modernization-plan.md` | 删除后恢复归档 | `PAGE_SIZE` shift size_t、`SYSCHK_pr` 展开、KernelSnitch 代码项 |
| `docs/archive/20260922-2231-native-modernization-candidates.md` | `docs/analysis/native-modernization-candidates.md` | 删除后恢复归档 | `iomem_map_span`/RAII 等边界记录 |
| `docs/archive/device-gates/20260922-2231-CPP06c...` | 原 device-gates | 归档 | KernelSnitch 阶段 kernel panic |
| `docs/archive/device-gates/20260922-2231-CPP07...` | 原 device-gates | 归档 | 第二次 KernelSnitch 阶段 panic |
| `docs/archive/device-gates/20260922-2231-CPP12k...` | 原 device-gates | 归档 | W2 spray 两次 panic |

恢复命令：

```sh
git show 064cd1e5^:docs/analysis/upstream-catch-up-20260913.md
git show 064cd1e5^:docs/analysis/native-modernization-candidates.md
git show 994ccf81^:docs/development/native-modernization-plan.md
git show aaaa6503 -- src/core/kernelsnitch/futex_hash.h src/core/kernelsnitch/kernelsnitch.h
```

## 5. 已删除代码记录

| 删除提交 | 删除路径 | 说明 |
|---|---|---|
| `2933b838` (2.0) | `core/src/backend/cve_2026_43499/leak/snitch_*`、`snitch_owner.hpp`、`core/src/tools/snitch_*_probe.cpp`、`app/src/main/assets/profile/kernelsnitch-6x.conf` | C++ 拆分后的 leak 模块整体下线 |
| `d2778a1a` (2.0.5) | 剩余 `core/src/backend/cve_2026_43499/leak/snitch_*` 与 `core/src/tests/snitch_*_test.cpp` | 当前 HEAD 删除 |
| `b0e9f499` | `src/core/backend/cve_2026_43499/leak/kernelsnitch.h`、`src/core/tests/kernelsnitch_scan_bounds_test.cpp` | 全量重写清空 |
| `d3de8764` | `src/core/kernelsnitch/{address_discovery,futex_hash,number_parse,scan_bounds,timeutils,utils}.h` | A3-2 移到 backend leak |
| `dfbb942a` | `core/src/tests/snitch_seam_test.cpp` | R2 命名清理 |

这些路径可从父提交恢复：

```sh
git show d2778a1a^:core/src/backend/cve_2026_43499/leak/snitch_setup.cpp
git show 2933b838^:core/src/backend/cve_2026_43499/leak/snitch_scan.hpp
git show b0e9f499^:src/core/backend/cve_2026_43499/leak/kernelsnitch.h
```

## 6. 当前本地与历史的差异

| 历史修复 | 当前 C 副本状态 |
|---|---|
| `aaaa6503` possible CPU 表尺寸 | **缺失 → 本次已回移** |
| `f8a50857` state labels | **缺失 → 本次已补** |
| `5dd1a3f9` slab shift size_t | 已有 `(size_t)PAGE_SIZE <<` |
| `994ccf81` leak memfd close | 已有 `close(rt->memfd_leak)` |
| `396e52d` direct map 上限 | 部分：本 C 副本无 `g_direct_map_end`，本次用 `IDENTITY_END` 封顶；`.ghostlock_iomem` 未移植 |
| `a0d6610f` 固定向量 | 当前只有最小 2048/4096 对测试，未恢复四组固定向量 |

## 7. 结论

- 冷机 leak 的根因不是新问题，而是 **2026-09-20 已修复的 possible-CPU 表尺寸没有同步到
  当前 `glkcommon` C 副本**。
- 历史记录完整可恢复：关键提交、归档文档、删除代码都在 git 中。
- 本次修复后冷机已越过 leak，进入 W1；新的失败点是
  `W1 scratch page quarantine failed`。

## 8. 证据

- 当前修复：[kernelsnitch-leak-fix.md](kernelsnitch-leak-fix.md)
- 当前门禁：[device-gates/20261021-1948-cold-43499-ksfix.md](device-gates/20261021-1948-cold-43499-ksfix.md)
- 历史提交：见 §3 的 hash。
