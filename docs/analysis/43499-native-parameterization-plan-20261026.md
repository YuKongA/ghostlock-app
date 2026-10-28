# 43499 Native 全参数化计划（P0a）

- 日期：2026-10-26；用户裁定：「禁止 Native 硬编码数值，全部参数化」；本批只做参数化+机制修复，不接 profile registry、不启用 pselect/TCP。
- 基线：HEAD `a9af5482`（2.0.11）+ 工作树未提交 Kotlin 重构；`core/kernel_internal.h` 现有约 40 个 `GLK43499_PHYS_*` 目标相关宏。
- 原则：Native 只做机制；所有**目标/设备/时序/尺寸**参数经 ABI 请求传入并 fail-closed 校验；Native 不得回退默认值。仅保留**结构性容量/协议编码**常量并注明理由（非目标参数）。

## 1. 宏分类与去向

### A. 目标/物理参数（必须移除，进请求结构）

| 宏 | 去向字段 |
|---|---|
| PAGE_SIZE / ALLOCATION_SIZE / SLAB_ORDER | `glk43499_placement_request`（已有 page_size/allocation_size；新增 slab_order） |
| MM_STRUCT_SIZE / OBJECTS_PER_SLAB / MAX_OBJECTS_PER_SLAB / MM_PARTIALS / PREPARE_SLABS | Placement（新增 mm_struct_size、mm_partials、prepare_slabs；objects_per_slab/max 已有或新增） |
| SNITCH_COLLISIONS | Placement（新增 snitch_collisions） |
| MAX_MEASUREMENT_ROUNDS | Placement（已有 scan_round_limit） |
| SKB_SEND_SIZE / SKB_RECLAIM_SENDS / FILL_BYTE | Landing/Page（新增 fill_byte；reclaim 相关已有 reclaim_payload_size/socket_send_buffer_size/reclaim_send_limit） |
| IDENTITY_START / IDENTITY_END | 删除；用请求里的 scan_lower/scan_upper 做范围校验，Native 不再内置地址空间边界 |
| LEAK_TAG_NIBBLE / LEAK_TAG_SHIFT | 协议编码常量，保留但注明“协议 wire 格式，非目标参数”（如用户仍要求参数化，再加字段） |
| MULTICAST_ARM_HOLD / MULTICAST_ARM_SEQUENCE（现缺失） | `glk43499_multicast_spec`（新增 arm_hold_ns、arm_sequence） |
| TCP_PUNCH_SPIN / TCP_POST_HOLD / TCP_ZC_ADDRESS_OFFSET | `glk43499_tcp_spec`（新增 punch_spin、post_hold_ns、zc_address_offset；mapping/frame 已有） |
| CONSUMER_MAX / BURST / LOCK_PI_SPIN_NS / POLL_US / SETUP_SETTLE_US / REQUEUE_EXPECTED / DISARM_SPIN_LIMIT | `glk43499_actor_request`（新增对应字段） |
| SELECT delay/timeout 参数（legacy 缺失） | `glk43499_pselect_spec`（新增 per_attempt_delay_ns / timeout 语义明确；pselect 本次不启用） |

### B. 结构性容量/协议（保留，注明非目标参数）

- 帧/映射缓冲容量上限（`MULTICAST_MAX_FRAME`、`TCP_MAX_MAPPING`、`TCP_MAX_FRAME`、`SELECT_MAX_NFDS`、`MAX_CONTEXT_ENTRIES`）：保留为编译期数组/缓冲容量，请求值必须 ≤ 容量并显式校验；注释标明“结构性上限，非目标参数”。
- `LEAK_TAG_*`：wire 协议编码，同上。

## 2. 改动清单

1. **ABI**：`core/placement.h`、`page.h`、`actor.h`、`primitive.h` 增字段（append-only，必要时 `GLK43499_ABI_VERSION` 升 1）；保持现有字段顺序/大小兼容（JNI 用 struct 版本号校验）。
2. **C core**：`kernel_internal.h` 删目标宏；`placement.c/page.c/actor.c/carrier_*.c` 从请求读参数并做范围校验（如 `scan_lower<scan_upper`、`objects_per_slab≥1`、`arm_hold≥0`、预算>0）；禁止任何默认回退。
3. **机制修复（同批）**：D6 reclaim 同 fd 设 `SO_SNDBUF/O_NONBLOCK` 并从同一 fd sendmsg；D3 kernel5 multicast ghost disarm（用请求里的 pid/flag 参数，不硬编码）；D5 的 arm_sequence/arm_hold 生效。
4. **JNI**：`jni43499.c` 编组新字段；缺失/越界 ⇒ 既有 fail-closed 回执（INVALID_ARGUMENT），不得乱码。
5. **Kotlin**：`Native43499Jni` DTO / `lifecycle` 各 Request 构建器显式携带全部参数；`Cve43499RequestFactory` 从 **debug-a301so.conf**（typesafe config，debug 专用）读取 A301SO 数值填入；缺参数 ⇒ fail-closed（不构造请求，Flow 报 inputs-incomplete）。
6. **测试**：C host 参数化单测（非法值拒绝、正确值透传、无默认回退、容量边界）；Kotlin DTO/JNI 编组单测 + debug 配置解析单测；NDK 构建零告警。

## 3. 验收判据

- `kernel_internal.h` 不再含目标/物理数值宏（grep 证据：仅剩结构性容量/协议常量，附理由注释）。
- 所有宏原值均能从请求字段获得；缺省/非法入参 fail-closed。
- `make -C core check` / `make -C core android`（或对应 Gradle 目标）绿；NDK 零告警。
- Kotlin 编译 + 相关单测绿；emulator UI 不退化（NotReady 仍禁用）。
- pselect/TCP 只完成参数化，不得在 debug 注入中启用。

## 4. 不在本批

- profile registry/HOCON 通用解析（debug 文件专用 loader 即可）。
- 真机门禁（P0c 之后再排）。
- Kotlin multicast payload/stamp/地址生成器（P0b，与本批参数化衔接）。
