# 附件 A2：P2 关键符号与使用点（48 头）

> 分析类（**≤8192 B**；本件与 A1 均由原单件附件**按配额拆分**而来）。**配套**：[contract-layer-p2-spec](contract-layer-p2-spec.md)（§1）· [附件 A1](contract-layer-p2-spec-annex-a1.md)（48 头依赖面全表）。判据形式 = D1。

## §A2 关键符号与使用点（逐头；其余头以 backend 实际 `grep` 使用点为准）

| 上层头 | 次数 | 关键符号（使用点示例） |
|---|---|---|
| `session/core_session.hpp` | 12 | `CoreSession`（`backend/cve_2026_43284_backend.hpp:13`、`:41` 的 `state_construct(CoreSession&)`） |
| `support/log.hpp` | 11 | 日志/平台宏层（中性，A3-2），被多数 backend TU 传递使用 |
| `contract/identity.hpp` | 10 | 组件 id / 可用性谓词 / identity 声明（ADR-0004 唯一权威） |
| `memory/payload_builder.h` | 9 | 写请求不可变描述 + `preserve_child` 布局选择 |
| `support/native_resource.hpp` · `support/decls.hpp` | 7 · 7 | 中性支持面：`read_first_line` · `log_sync` · `fail_stop_dirty_race` · `disable_rseq_for_thread` |
| `profile/document.hpp` | 6 | `profile::Document`（`cve_2026_43284_backend.hpp:52` bind 入参） |
| `memory/target.h` | 6 | `TargetProfile`（`cve_2026_43499_state.hpp:26` state 成员） |
| `profile/schema.hpp` · `contract/capabilities.hpp` | 5 · 5 | `BindCode`/`FieldSpec`（具名拒绝语义）· 能力声明 |
| `support/status.hpp` · `support/timing.hpp` | 5 · 4 | 状态码 · 时序常量 |
| `race/pi_race.h` | 4 | `race::PiRace`（`cve_2026_43499_state.hpp:29`；route 入参 `select_stack_route.cpp:12`） |
| `contract/stage_result.hpp` | 4 | `contract::StageResult`（`cve_2026_43284_backend.hpp:14`、`:56` 返回值） |
| `session/runtime_config.h` · `profile/runtime_struct_offsets.h` · `platform/device_facts.hpp` | 4 · 4 · 4 | 运行期配置 · 结构偏移常量 · 设备事实 |

- **口径**：本表实测 **48 头**；task-89 报 **47**，差异 1 处待核（spec §8 U-1）。
- **未在上表逐符号列出的头**：多为**类型/常量声明**或**仅传递包含**；P2 实现时逐个补齐（**缺一即 59 文件之一编译失败**）。

## §A3 本表的生成/复核方法

- **符号提取（逐头复核）**：对每个头，用头内声明名到 `backend/**` 里找使用点，例：

```sh
grep -rn "CoreSession" src/core/backend --include=*.cpp --include=*.hpp | head   # 12 次包含，使用点含 state_construct
grep -rn "TargetProfile" src/core/backend --include=*.cpp --include=*.hpp | head  # state 成员（cve_2026_43499_state.hpp:26）
grep -rn "PiRace" src/core/backend --include=*.cpp --include=*.hpp | head         # state 成员 + route 入参
```

- **依赖面（次数列）的生成命令**见 [附件 A1](contract-layer-p2-spec-annex-a1.md) 末尾（解析 `#include` 并解析到已禁用上层头后聚合）。
- **口径注（U-1，**已结清：48 为准**）**：`grep -rhoE '#include "[^"]+"' src/core/backend | sort -u` = **102** 个去重 include ⇒ 非 backend/lib **52** ⇒ 减去 4 个位于 `backend/cve_2026_43499/leak/` 内部的自有头（`kernelschnitch.h` / `futex_hash.h` / `scan_bounds.h` / `timeutils.h`）⇒ **上层头 = 48**（spec 的 48 正确；task-89 的 47 少算 1）。
- **未在上表逐符号列出的头**：多为**类型/常量声明**或**仅传递包含**；P2 实现时逐个补齐（**缺一即 59 文件之一编译失败**）。
