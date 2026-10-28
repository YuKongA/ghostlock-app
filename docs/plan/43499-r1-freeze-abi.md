# 43499 R1 附件：ABI v4 修订矩阵（删 reserved + A 全参数化）

- 状态：R1 修订件 + **R2 实现态（2026-10-27，writer impl-43499）**：§3 after 矩阵已由 `_Static_assert` 与 host probe 逐项复核一致。
- 基线：HEAD `a9af5482`（v4 半成品）；本件给 before（当前工作树）/after（R2 目标）矩阵，**R2 以 after 为唯一 ABI**。

## 1. reserved 精确清点（6 个 ABI 头，共 **24 处声明**；全删）

| 头 | 条数 | 行号 |
|---|---|---|
| abi.h | 1 | 53 |
| result.h | 3 | 73,75,85 |
| placement.h | 3 | 37,45,58 |
| page.h | 4 | 23,42,57,58 |
| actor.h | 6 | 27,40,53,54,69,70 |
| primitive.h | 7 | 30,42,46,59,77,88,94 |

> 扫描器 `reserved` 计数=24（声明级，剥注释/字符串后）；当前工作树 `actor.c` 另有 3 处对 reserved 字段的赋值，R2 随字段删除同步删除（不计入声明清点）。

## 2. before（当前工作树 v4）sizeof/offset 矩阵

| 结构 | sizeof v4(含reserved) | sizeof v4 删reserved | Δ |
|---|---|---|---|
| glk43499_abi_header | 24 | 8 | 16 |
| glk43499_call_result | 88 | 32 | 56 |
| glk43499_close_result | 136 | 48 | 88 |
| glk43499_placement_request | 184 | 152 | 32 |
| glk43499_placement_facts | 128 | 64 | 64 |
| glk43499_placement_result | 216 | 96→136 | 120 |
| glk43499_page_bytes | 80 | 32 | 48 |
| glk43499_landing_request | 152 | 80 | 72 |
| glk43499_landing_receipt | 136 | 72 | 64 |
| glk43499_landing_result | 224 | 104 | 120 |
| glk43499_actor_setup | 152 | 128 | 24 |
| glk43499_actor_prepare_receipt | 104 | 40 | 64 |
| glk43499_actor_execute_receipt | 128 | 56 | 72 |
| glk43499_actor_prepare_result | 192 | 72 | 120 |
| glk43499_actor_execute_result | 216 | 88→288 | 128 |
| glk43499_primitive_addresses | 56 | 24 | 32 |
| glk43499_multicast_spec | 152 | 88 | 64 |
| glk43499_pselect_spec | 144 | 72 | 72 |
| glk43499_tcp_zerocopy_spec | 160 | 112 | 48 |
| glk43499_primitive_spec | 256 | 128 | 128 |

| 代表字段 offset | v4 | 删 reserved | Δ ||---|---|---|---|
| glk43499_abi_header.struct_size | 0 | 0 | 0 |
| glk43499_call_result.call_status | 24 | 8 | 16 |
| glk43499_placement_request.page_size | 24 | 8 | 16 |
| glk43499_landing_request.expected_page_base | 24 | 8 | 16 |
| glk43499_actor_setup.owner_cpu | 24 | 8 | 16 |
| glk43499_primitive_spec.kind | 24 | 8 | 16 |

## 3. after（R2 目标：删 reserved + A 全参数化）sizeof 矩阵

```
glk43499_abi_header 8
glk43499_call_result 32
glk43499_close_result 48
glk43499_placement_request 216
glk43499_placement_facts 64
glk43499_placement_result 136
glk43499_page_bytes 32
glk43499_landing_request 80
glk43499_landing_receipt 72
glk43499_landing_result 104
glk43499_actor_setup 128
glk43499_actor_prepare_receipt 40
glk43499_actor_execute_receipt 56
glk43499_actor_prepare_result 72
glk43499_actor_execute_result 288
glk43499_primitive_addresses 24
glk43499_multicast_spec 112
glk43499_pselect_spec 80
glk43499_tcp_zerocopy_spec 120
glk43499_primitive_spec 136
```

- **landing_receipt 尺寸修正**：before 136B → after **72B**（删除 reserved32+reserved[8] 共 64B；字段顺序不变）。R2 在 page.h 加 `_Static_assert(sizeof(struct glk43499_landing_receipt) == 72)`。
- R2 新字段分布：placement_request 216B（+8×u64）、multicast_spec 112B（+max_frame+ghost_pid+futex_waiter_flag+disarm_required+对齐 pad）、pselect_spec 80B（+max_nfds）、tcp_zerocopy_spec 120B（+max_mapping/max_frame，−fill_byte）。
- **R2 实测（host）**：`clang -std=c11 -I app/src/main/cpp/gl43499/core tools/abi_layout_probe.c -o /tmp/abi_layout_probe && /tmp/abi_layout_probe` → 20 个 sizeof 与本表 §3 全等；代表 offset：identity_start=152、max_frame=88、disarm_required=104、max_nfds=72、max_mapping=104。NDK 侧每头 `_Static_assert(sizeof==矩阵值)` 强制漂移即编译失败。
- **NDK 侧 `_Static_assert(sizeof==矩阵值)`**：在每个 ABI 头末尾（结构定义后）加 `_Static_assert(sizeof(struct glk43499_X) == <值>, "ABI v4 layout drift");`；值为本 §3 表（host 编译同样断言；矩阵命令见 §5）。

## 4. struct_size / abi_version 单源（请求/嵌套/输出三侧）

| 侧 | 填写者 | 值来源 | 校验 |
|---|---|---|---|
| 请求结构（placement/landing/actor/setup/primitive_spec） | JNI `GLK43499_INIT_ABI`（jni43499.c:83-84） | C `sizeof(具体结构)` + `GLK43499_ABI_VERSION=4` | Native `glk43499_internal_valid_header` 必须 `struct_size==sizeof && version==4`，否则 BAD_ABI/INVALID_ARGUMENT |
| 嵌套（page_bytes/addresses/union 分支） | JNI 同宏（land bytes/spec 嵌套） | C `sizeof` | 同上；未选 union 分支与尾零检查 |
| 输出（call_result/close_result/*_facts/*_receipt） | JNI `glk43499_init_call_result`（填**外层输出结构** sizeof）+ core `internal_set_*` | C `sizeof` | core `internal_output_abi` 严格 `struct_size==sizeof(*result)`，漂移即 BAD_ABI；Kotlin 不读 size/version，只读语义字段 |

Kotlin 侧**不硬编码** struct_size/abi_version；ABI v4 同构建 JNI↔.so 自洽，无跨版本兼容。

## 5. abi.h 契约文本与 `ABI_HEADER_INIT` 修改点

- `abi.h:48` KDoc「reserved 必须全零；未来只允许结构尾部追加字段」→ 改为「**ABI v4 起无 reserved 槽；字段只允许 append-only 追加**」；
- `abi.h:53` 删除 `uint64_t reserved[2];`；`abi_header` = `{struct_size:u32, abi_version:u32}` = 8B；
- `GLK43499_ABI_HEADER_INIT`（abi.h:57-59）→ `{ (uint32_t) sizeof(type_), GLK43499_ABI_VERSION }`（不再有 reserved 初始化）；
- `core_internal.h` 的 `glk43499_internal_valid_header`/`zero_u64` 移除 reserved 检查，保留 `struct_size/version` 严格校验与「未选 union 分支尾零」检查（后者非 reserved 槽）；
- 历史 v3 注记：`result.h:57-73` 仅加一行「ABI v4 起 reserved 槽删除，同构建内无需兼容旧二进制」，**不重写历史正文**。

## 6. 可复现矩阵命令（host）

```sh
# 当前/目标头复制到临时目录（R2 前）后：
clang -O0 -I <headers-dir> tools/abi_layout_probe.c -o /tmp/abi_probe && /tmp/abi_probe
```
`tools/abi_layout_probe.c` 为 R2 新增 host 探针（打印 sizeof/offsetof；矩阵值回填 static_assert）。产出与本文 §2/§3 对拍；漂移即 R2 gate FAIL。

## 10. R3 增量 ABI（2026-10-27；R3a Native + R3b 双 fill）

> 本节是 R3 后**当前工作树**的 ABI 权威（§3 的 R2 矩阵是历史）。
> 探针命令：`clang -std=c11 -I app/src/main/cpp/gl43499/core tools/abi_layout_probe.c -o /tmp/abi_layout_probe && /tmp/abi_layout_probe`。

### 10.1 R3 after sizeof 矩阵（host 实测 20 项）

```text
glk43499_abi_header                 8
glk43499_call_result               32
glk43499_close_result              48
glk43499_placement_request        216
glk43499_placement_facts           64
glk43499_placement_result          136
glk43499_page_bytes                32
glk43499_landing_request          104
glk43499_landing_receipt           88
glk43499_landing_result           120
glk43499_actor_setup              128
glk43499_actor_prepare_receipt     40
glk43499_actor_execute_receipt     88
glk43499_actor_prepare_result      72
glk43499_actor_execute_result     288
glk43499_primitive_addresses       24
glk43499_multicast_spec           112
glk43499_pselect_spec             104
glk43499_tcp_zerocopy_spec        176
glk43499_primitive_spec           192
```

### 10.2 R3 新字段 offset（host 实测）

| 结构.字段 | offset |
| --- | --- |
| landing_request.page_accept_required / offset / bit | 80 / 88 / 96 |
| landing_receipt.page_accept_value / checked / ok | 72 / 80 / 84 |
| actor_execute_receipt.carrier_operations / error / last_step / last_errno / punch_failed | 56 / 64 / 68 / 72 / 76 |
| actor_execute_receipt.ghost_disarm_result / errno | 80 / 84 |
| multicast_spec.arm_hold_ns / arm_sequence / max_frame / ghost_pid / futex_waiter_flag / disarm_required | 72 / 80 / 88 / 96 / 100 / 104 |
| pselect_spec.max_nfds / enter_delay_ns / fd_reserve_stride / select_use_timeval | 72 / 80 / 88 / 96 |
| tcp_zerocopy_spec.punch_spin / post_hold_ns / zc_address_offset / max_mapping / max_frame | 80 / 88 / 96 / 104 / 112 |
| tcp_zerocopy_spec.arm_sequence / zc_length_offset / zc_task_offset / zc_lock_offset / punch_phase_spin | 120 / 128 / 136 / 144 / 152 |
| tcp_zerocopy_spec.send_fill_byte / mapping_fill_byte | 160 / 168 |
| primitive_spec.kind / value(union) | 8 / 16 |

### 10.3 不变量（R3 同步）

- **append-only**：R3 只在结构尾部追加字段；未删任何 R2 字段（`landing_request.fill_byte` 保留，
  其语义变更见 [fields](43499-r1-freeze-fields.md) §7.3）。
- **三侧单源**：JNI 以 `sizeof(结构)` 填 `struct_size`（`GLK43499_INIT_ABI`），C 以
  `GLK43499_ABI_HEADER_INIT` 填输出；Kotlin 不硬编码 struct_size/abi_version。
- **静态断言**（NDK 与 host 同触发）：
  `landing_request==104`、`landing_receipt==88`、`landing_result==120`、`actor_execute_receipt==88`、
  `actor_execute_result==288`、`multicast_spec==112`、`pselect_spec==104`、
  `tcp_zerocopy_spec==176`、`primitive_spec==192`、`primitive_addresses==24`。
- **union 尾零**：Actor prepare 仍拒绝未选中分支的非零尾字节；tcp 分支新增双 fill 后
  primitive_spec 尾字节全零要求不变。
- **scanner r2**：allowlist 已按 R3b 新行号重生成并重签（allowlist sha256
  `07c8cad4a1a526fd80090314ab571ba3e918f33aed8ed5fe82db3b585f063e2c`，script sha256
  `2beeb25e43f68dd54fe7f1046bf9178bc479b1c4faeb288081e8ae854a613bba`），`--gate r2` exit 0。

### 10.4 UNVERIFIED

- host C harness（V1–V12 可执行负例）仍未建：ABI/校验负例目前由 core 内联函数 + Kotlin
  generator 单测 + 代码审读保证；偿还=P0c 前（owner=Lead）。

### 10.5 R3d ABI diff（2026-10-27）

- **R3d 不修改 ABI**：无 reserved 回填、无字段 append、无 sizeof/offset 变化；
  §3 after 矩阵与 §10.3 静态断言集合原样有效。
- 三侧单源不变：JNI 仍以 `sizeof` 填 struct_size；Kotlin 仍不硬编码 struct_size/version；
  R3d 的 Kotlin 数据平面（Credential/fast-repair/W1b/W3）不进入 ABI。
- ABI probe（`tools/abi_layout_probe.c`）未重跑出新差异（Native 源未改动）；真机 ABI
  仍待 P0c。多写链所需 target 由 `Cve43499WriteTargetSource` seam 提供，不是 ABI 字段。

### 10.6 R3e perf ABI（2026-10-27；ABI v4 不变、append-only 新模块）

- 新增 `core/perf.h`（公共头不含 Linux 头；`api.h` 聚合）：
  - `struct glk43499_perf_open_request` = **128**（abi 8 + 15×u64：pid/cpu/attr_type/attr_config/attr_sample_period/attr_sample_type/attr_sample_regs_intr/attr_flags/attr_disabled/exclude_user/hv/idle/page_size/ring_pages/auto_enable）。
  - `struct glk43499_perf_open_result` = **32**（call）。
  - `struct glk43499_perf_collect_receipt` = **48**（data_offset/data_size/data_head/data_tail_before/bytes_copied/tail_moved；head/tail 为单调绝对位置）。
  - `struct glk43499_perf_collect_result` = **80**（call+receipt）。
  - close 复用既有 `glk43499_close_result`=48（不新增）。
- 既有 20 结构 sizeof/offset **全部不变**（`_Static_assert` + host `tools/abi_layout_probe.c` 实测：新增 4 项 128/32/48/80）。
- 校验：三入口输出/输入 ABI 前缀 strict `struct_size==sizeof`+`abi_version==4`；`perf_open_request` 域校验（cpu=-1、pid/auto_enable/page_size/ring_pages/乘法溢出）；**无状态句柄**（不校验 OPEN/COLLECTED，close 唯一性由 Kotlin owner）。
- 机读：`glk43499_perf_open/collect/close`；JNI `openPerfSampler/collectPerfSamples/closePerfSampler`（三态协议）。
