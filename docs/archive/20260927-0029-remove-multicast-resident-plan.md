> 恢复说明（2026-10-06）：原始路径 ��删除提交 ����；恢复来源 git show c50e3ef44735abd0ac4c54315c80063dea1dc39f^:docs/analysis/remove-multicast-resident-plan.md；内容为删除前原文，未改动。

# 删除 multicast resident 写入器与 fake BSS 字段 计划（2026-09-26）

## 现状与基线

- 分支 `exp/multicast-2`（从回退后的主线 `a5a7243` 切出，工作树相对 `remote/very-not-stable-dev`
  仅多两个 Kotlin 文件）。
- 5.15 multicast 实际命中路径是 **one-shot**（`do_kernel5_fake_lock_route`），证据：
  - `attack_write`（`session/backend/cve_2026_43499_backend.cpp:489`）先试 `M::resident_write`，
    只有返回 `nullopt` 才落到 one-shot；
  - `MulticastPolicy::resident_write`（`route/multicast_waiter_route.cpp:457`）在
    `profile.multicast_resident()` 为假时返回 `nullopt`；
  - 内置 5.15 profile 无 `multicast_resident` 键，且 Kotlin `ProfileResolver.nativeValue` 不映射
    该键（落到被 `validateMerged` 拒绝的顶层键）→ 值恒 0（`known-bugs-2026-09-26-multicast.md` BUG-1）。
  - `remote/very-not-stable-dev` 后期全部 PASS 门禁（B3/B4×5/RACE-LIFETIME）成功日志均为 one-shot；
    resident 的 `5.x resident writer ready` 从未出现。
- resident 在 A301SO 上从未验证成功（作者原版 PoC 同样写不中；`initial_research.md` 更正）。
- resident 依赖的 `fake_lock_offset`/`fake_task_offset` 需要 `z_pagemap_global` 符号 + 人工偏移，
  是 extractor 最难提取、也最不可移植的部分（用户 2026-09-26 决策删除）。
- legacy（v1 `offsets.json`）在 `remote/main` 中**不含任何 mcast 字段**；local 的 mcast 支持是
  `8ea6d4c`（GLK1 v4）后加的，属越界，本计划一并清理。

## 目标与约束

### 目标

1. 删除 resident 写入器全部实现与其 route hook：`MulticastWaiterRoute` 类/`.h`、`resident_route()`、
   `kernel5_resident_{start,write,stop}`、`MulticastPolicy::resident_write`/`w1_resident_repair`。
2. 删除 resident 专属 profile 数据：`multicast_resident`、`mcast_fake_lock_offset`、
   `mcast_fake_task_offset`、`mcast_lock_slots_offset`、`mcast_lock_slot_count`、
   `mcast_lock_slot_stride`、`off_mcast_fake_bss`，以及 resident 时序参数
   `multicast_ready_timeout_ms`/`multicast_post_requeue_settle_us`/`multicast_post_adjust_settle_us`
   （仅 resident worker 使用）。
3. 同步删除 extractor 的 mcast fake 几何推导与 golden 断言、Kotlin 双侧配置、内置 profile、测试、
   文档。
4. 保持 one-shot multicast、select、tcp 行为与 wire 语义不变。

### 非目标

- 不删 route、不改 `RouteKind`/`kRouteCatalog`（multicast 保留）。
- 不删 one-shot 路径与 `w2_fast_repair_*`、`w1_scratch_repair`（one-shot 在用）。
- 不改 select/tcp profile 字段、页构造与算法。
- 不改变 v1 legacy 对 pselect/tcp 与通用符号的解析；仅移除本项目后加的 multicast 支持（见 D4）。
- 不声称任何新设备受支持。

## 设计

### D1：删除 Native resident 实现

- `route/multicast_waiter_route.h`：删 `MulticastWaiterRoute` 类、`ResidentState`、`can_rollback_prearm`/
  `requires_fail_stop`、`resident_route()`；只保留 one-shot 所需的声明。
- `route/multicast_waiter_route.cpp`：删文件级 `multicast_resident_route` 实例、两个 worker、
  `start/write/stop`、`multicast_waiter_stamp`、resident disarm/destroy、`kernel5_resident_*` 包装、
  `MulticastPolicy::resident_write`/`w1_resident_repair`；保留 `do_kernel5_fake_lock_route`（one-shot）
  与 `w2_fast_repair_*`。
- `route/route_policy.hpp`：从 `MiddlewarePolicy` concept 与 `RoutePolicyDefaults` 删除
  `resident_write`/`w1_resident_repair`；保留 `w2_fast_repair_*`。
- `route/route_api.hpp`：删 `kernel5_resident_write` 声明（若仍有）。
- `session/backend/cve_2026_43499_backend.cpp`：
  - `:489` 删除 `M::resident_write` 前置分支，`attack_write` 直接 heap spray + `run_middleware_route`；
  - `:349` `w1_scratch_repair` 去掉 `multicast_resident()` 早退（恒执行 one-shot scratch repair）；
  - `:396` 去掉 `multicast_resident()` 条件（恒 `w1_attempts = 1`）；
  - `:409` 删除 W1 失败时的 `kernel5_resident_stop()`。
- `session/exploit_session.{hpp,cpp}`：删 `release_resident_heap()`（其函数体为通用
  `close_reclaim_sockets`+`cleanup_page_prepare_state`，删除后由既有清理路径覆盖；若他处仍需，改为
  直接调用那两个 support 函数）。

### D2：删除 profile 字段（Native + wire）

- `profile/model.h`：`kernel_offsets` 删 `multicast_resident`、`mcast_fake_lock_offset`、
  `mcast_fake_task_offset`、`mcast_lock_slots_offset`、`mcast_lock_slot_count`、
  `mcast_lock_slot_stride`、`off_mcast_fake_bss`；删 `multicast_resident()` 访问器；
  `MulticastWaiterLayout` 删 `fake_lock_offset`/`fake_task_offset`/`lock_slots_offset`/
  `lock_slot_count`/`lock_slot_stride`/`fake_bss_image_offset`；`multicast_layout()` 同步；
  resident 时序访问器（`multicast_ready_timeout_ms`/`post_requeue_settle_us`/`post_adjust_settle_us`）
  删除。
- `profile/binary.cpp`：删对应字段表项。
- `profile/execution`（若有 resident 时序字段）同步。

### D3：Kotlin 双侧同步（必须逐字一致）

- `profile-core/.../route/MulticastConfig.kt`：删 `fakeBssImageOffset`/`resident`/时序字段与
  `MulticastGeometry` 的 fake/slot 字段；`entries()`/`apply()`/`from()`/`EMPTY` 同步。
- `ProfileResolver.nativeValue`：确认无遗留 resident 分支（本就不映射）。
- `ui/FieldLabels.kt` + `res/values*/strings.xml`：删 resident/fake/slot 标签。
- `app/data/LegacyProfileConverter.kt`、`AndroidProfileConfigController.kt` 中 resident 字段映射删除。
- 内置 `app/src/main/assets/kernel_profiles/5.15.189-…conf`、`5.15-template.conf`：删
  `fake_lock_offset`/`fake_task_offset`/`lock_slots_offset`/`lock_slot_count`/`lock_slot_stride`/
  `mcast_fake_bss`。

### D4：从 legacy 移除 multicast（恢复 remote/main 语义）

- 事实：`remote/main` 的 `offsets_json.c` 不含任何 mcast 字段；local legacy 的 mcast 支持是
  `8ea6d4c`（GLK1 v4 重构）后加的，违背“新 route 不改 legacy”。
- `src/core/legacy/offsets_json.cpp`：删除 `decode_multicast_branch()`、`g_profile_map` 中的
  `mcast_*` 项、`decode_text` 中对 `route.multicast_waiter` 分支的解析与 `kRouteMulticastWaiter`
  赋值；legacy 恢复为“只解析 pselect/tcp + 通用符号”。
- `app/src/main/kotlin/.../LegacyProfileConverter.kt`：删除 `McastFields`、`MulticastRouteCodec`、
  `moveMcast`、v1→`multicast_waiter` 的路由推断（`:346-347`）及其它 mcast 引用。
- `kernel_offsets` 仍保留 one-shot 使用的 `mcast_waiter_off`/`mcast_buffer_size`/
  `mcast_task_offset`/`mcast_lock_offset`（GLK1/HOCON 配置需要）；legacy 不再为它们建立 v1 映射。
- 测试：删 `offsets_json_test.cpp` 的 multicast 用例；新增 remote/main 参考格式向量
  （`check_remote_main_reference_format`）钉死 100% 字段映射。

### D4.1：legacy ↔ remote/main 字段映射核对（100%）

对照 `remote/main:src/core/offsets_json.c` + `src/kernels/offsets.h`：

| remote/main 来源 | 字段 | 现 legacy |
|---|---|---|
| 顶层标量 | `kernel_phys_load`、`pselect_waiter_shift`、`compact_waiter`、`mm_struct_sz` | `g_profile_map` 全覆盖 |
| `symbols{}` | 9 个 `off_*`（init_task/init_cred/root_task_group/selinux_enforcing/selinux_blob_sizes/security_hook_heads/slide_nfulnl_logger/slide_loggers_0_1/slide_boot_id） | `g_symbol_map` 9/9 + `symbols{}` 嵌套读取 |
| `struct_fields{}` | 15 个 `task_*` | `g_task_map` 15/15 + `struct_fields{}` 嵌套读取 |

现 legacy 严格贴合 remote/main：**不解析** `off_empty_zero_page`、`kernel_major`、`recommend_shizuku`
（前两者 remote/main 的 struct 里没有；后者当时不存在），分别默认 `kernel_major = 6`、
`recommend_shizuku = 0`；native `infer_route` 与 Kotlin `inferRoute` 去掉 5.x→multicast 分支，
只保留 `compact_waiter → tcp / else select`。仍保留的是当前 schema 的本地过渡处理
（`cred_*`、`kernelsnitch{}`、`route/fallback`、`execution`），它们不来自 remote/main 的字段集。

守卫：`offsets_json_test.cpp::check_remote_main_reference_format` 用 remote/main 原始 JSON 形状
（无 `schema_version`、无 `route`）走 `profile_json::select_entry`+`fill_entry`，断言 4 标量 + 9 symbols +
15 task 全部 1:1 映射。Kotlin 侧：`LegacyProfileConverter` 仅对**带 release 的完整 legacy 文档**
默认 `kernel_major=6`（稀疏 override 不注入，避免覆盖内置 5.15 的 `kernel_major=5`）。

### D5：extractor

- `tools/extract_rs/src/derive.rs`：删 `MULTICAST_5X_FAKE_*`/`LOCK_SLOTS` 常量与其对
  `fake_lock_offset`/`fake_task_offset`/`lock_slots_offset` 的输出。
- `tools/extract_rs/src/main.rs`：删 `mcast_fake_bss: kallsyms::unique(&symbols, "z_pagemap_global")`。
- `tools/extract_rs/src/report.rs`：删 `mcast_fake_bss` 字段与 conf 输出、更新 golden 断言
  （`report.rs:464/471/484/517-519`、`derive.rs:155-156/836-838`）。

### D6：测试与文档

- Native 测试：`multicast_waiter_route_test.cpp`（删 resident 用例，保留 one-shot 向量）、
  `profile_test.cpp`、`profile_binary_test.cpp`（删删字段断言）、`heap_context_test.cpp`（若含 resident）。
- Kotlin 测试：`NativeProfileDocumentTest.kt`、`ProfileRoundTripTest.kt`、`OffsetMatchingTest.kt`、
  `RouteCatalogAgreementTest.kt`（route 不变，确认不受影响）。
- 文档：`docs/kernel_profiles/PROFILE_SCHEMA*.md`、`defaults*.md`、`templates`、
  `docs/development/adding-a-component.md`、`src/core/README.md`、`AGENTS.md` 中 resident 字段描述。

## 数据流/控制流差异

```mermaid
flowchart TD
  subgraph Old[当前]
    A[attack_write] --> B{"M::resident_write"}
    B -- "multicast_resident=1" --> R[resident: BSS fake lock/task + slots]
    B -- nullopt --> O1[heap spray + one-shot multicast]
  end
  subgraph New[本计划]
    A2[attack_write] --> O2[heap spray + one-shot multicast<br/>do_kernel5_fake_lock_route]
    O2 --> F[w2_fast_repair / w1_scratch_repair 保留]
  end
```

不变量：one-shot 写原语、select/tcp、`prepare_kernel_page`/`w2_fast_repair` 顺序与语义不变；
非 resident profile 字段的 wire 布局不变。

## 兼容性与回滚

- GLK1 wire：字段表按名删除；旧 `.bin` 携带已删键时的行为需在验证矩阵核对（未知键应被忽略而非拒绝）。
- 内置 profile 与 extractor 输出同步删除，避免候选 HOCON 再产出这些键。
- 回滚：恢复 resident 实现与字段（git revert），不迁移用户数据。

## 验证矩阵

| 批次 | 检查 | 预期 |
|---|---|---|
| B1 Native 删除 | `make -C src native-host-tests` | 通过 |
| B1 | `make -C src ghostlock`（NDK） | 零告警 |
| B1 | `make -C src lint-tidy` | 0 findings |
| B1 | `tools/cmp_disasm.py <baseline> build/native/ghostlock` | 8 函数 IDENTICAL 或已复核差异（`do_one_write`/`do_kernel5_fake_lock_route` 可能因删前置分支变化，需逐条复核顺序不变量） |
| B2 字段删除 | Native 字段表 ↔ Kotlin config 一致性测试（`route_catalog_test`/`RouteCatalogAgreementTest`/`profile_binary_test`） | 同一列表 |
| B2 | `cargo test --release --manifest-path tools/extract_rs/Cargo.toml` | 通过（golden 更新） |
| B2 | `./gradlew :app:testDebugUnitTest` | 通过 |
| B3 行为 | 真机门禁：冷机、固定 CPU 对、单 route（multicast）、KernelSU 未加载 | W1/W2/W3 写验证通过；日志归档 `docs/analysis/device-gates/*.md` |
| B3 | 回归：one-shot 成功率抽样（N 次冷机） | 记录命中率，作为删除后的基线 |

> 攻击关键路径改动：`cmp_disasm`（8 函数）+ 真机门禁 + 门禁记录缺一不可。

## 明确保留

- `do_kernel5_fake_lock_route`（one-shot）、`prepare_kernel_page`、`w2_fast_repair_*`、
  `w1_scratch_repair`、`stash/activate_prebuilt_page`。
- `select_stack_route` / `tcp_zerocopy_route`；`race/**` 线程模型。
- `RouteKind`/`kRouteCatalog`、GLK1 版本、`--format conf` 的未验证候选语义。
- one-shot 需要的 wire 字段 `mcast_waiter_off`/`mcast_buffer_size`/`mcast_task_offset`/
  `mcast_lock_offset`（GLK1/HOCON 配置路径）。
- v1 legacy 对 pselect/tcp 与通用符号的解析；multicast 从 legacy 移除（恢复 `remote/main` 语义）。
- `g_exploit_session`、`g_direct_map_end` 约定。

## 进度

- [x] Explore：确认 resident 从未被启用/验证；one-shot 为唯一成功路径；字段与字段使用点已列。
- [x] Design：本计划（获用户认可：hook 彻底删、时序参数删、legacy multicast 整体移除）。
- [x] B1：删除 Native resident 实现 + hook + backend 调用。验证：host tests PASS、NDK 零告警、lint 0 finding、
  `cmp_disasm` 复核（`do_one_write` 仅删 resident optional 快速路径 6 条；worker MISSING 属预期；其余为地址重定位）。
- [x] B2：删除 profile 字段并双侧同步（Native/Kotlin/extractor/assets/tests/docs）。验证：host tests PASS、
  NDK 零告警、lint 0 finding、`./gradlew :app:testDebugUnitTest` 全绿、`cargo test` 全绿、golden 更新。
- [x] B3：真机门禁 **PASS**（用户 2026-09-26 确认）；归档记录待补（`docs/analysis/device-gates/`）。
  legacy↔remote/main 字段映射核对完成并加守卫测试（D4.1）。
