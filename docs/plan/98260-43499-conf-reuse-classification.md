# 43499 旧内置 conf → 98260 参数复用分类矩阵（细表）

> 2026-10-28。目的：把 43499 debug 内置 `app/src/debug/assets/profile/43499/debug-a301so.conf` 的每个内容项分类，判断哪些语义可给 98260 用；并把「BackendSpec 需声明偏移参数」落成 `BackendParameterSchema`。
> 权威：本文只做分类；实际值必须按目标内核（PLZ110 6.12.69）重新推导，**禁止照抄 43499 的 5.15 数值**。

## 0. 图例

| 标记 | 含义 |
|---|---|
| ✅ | 语义/字段可复用（值需按 98260 profile 重填） |
| 🔧 | 概念可映射或需改造（不可直接复用数值/算法） |
| ❌ | 43499 专用（route/write-chain/perf 等），98260 不用 |
| ℹ️ | 通用 UI/流程/基础设施项（不是内核偏移） |

## 1. 元信息 / 设备身份

| 43499 conf 项 | 分类 | 98260 对应 |
|---|---|---|
| `release` | ✅ | 98260 profile `release`（同字段语义，值不同） |
| `schema_version` | ✅ | 98260 `schema_version`（当前 1） |
| `kernel_major` | ✅ | 98260 profile `kernel_major`（6） |
| `recommend_shizuku` | ℹ️ | 98260 由 `uidRequirement=SHELL` 声明，不需要该 flag |
| `kernel_phys_load` / `kernel_phys_offset`（null） | ❌ | 43499 generator 物理地址模型专用 |

## 2. `route.multicast_waiter`（43499 注入路由几何）

| 项 | 分类 | 说明 |
|---|---|---|
| `waiter_off` / `buffer_size` / `task_offset` / `lock_offset` / `compact_waiter` | ❌ | 43499 multicast waiter 写入链专用；98260 用 POSIX timer UAF，无 waiter 布局 |

## 3. `offset`（静态符号/锚点）

| 项 | 分类 | 说明 |
|---|---|---|
| `selinux_enforcing` | 🔧 | 与 SELinux 状态相关，但 98260 用 `selinux_state_static` + blob SID；值不复用 |
| `selinux_blob_sizes` | 🔧 | 仅 SELinux blob 布局概念可参考；98260 用固定 `selinux_blob_off` |
| `init_task` / `init_cred` / `root_task_group` / `empty_zero_page` / `security_hook_heads` | ❌ | 43499 写链/凭据伪造对象；98260 用 `root_user`/`init_user_ns`/`init_ucounts`/`init_groups` |
| `slide_nfulnl_logger` / `slide_boot_id` / `slide_loggers_0_1` | ❌ | 43499 KASLR slide 锚点；98260 用 `static_timer_site`/`timer_site_low21` + 运行时 `g_slide` |

## 4. `task_struct`（公共 ABI 布局）

| 项 | 分类 | 说明 |
|---|---|---|
| `cred` | ✅ | 对应 98260 `off_task_cred`（字段语义一致；**值必须按 6.12.69 重填**） |
| `real_cred` | 🔧 | 同属公共 ABI 概念；98260 只需 `task->cred`，不需要 real_cred（可声明 optional） |
| `prio` / `normal_prio` / `sched_task_group` / `pi_lock` / `pi_waiters` / `pi_top_task` / `pi_blocked_on` | ❌ | 43499 PI/waiter 链；98260 不使用 |
| `pid` / `tgid` / `atomic_flags` / `comm` / `tasks` / `seccomp` | ❌ | 98260 不以 task_struct 字段做 trace/判定（seccomp 属 43499 W3） |

## 5. `cred`（公共 ABI 布局 + 43499 写链特化）

| 项 | 分类 | 98260 对应 / 说明 |
|---|---|---|
| `caps_offset` | ✅🔧 | fake cred caps 起始偏移概念可复用；98260 当前**硬编码 0x30**，已登记待提升 `fake_cred_caps_off` |
| `copy_size` | ✅ | 对应 98260 `cred_req`（cred 分配请求尺寸；值不同） |
| `caps_count` | ✅ | fake cred cap 组数/词数概念；98260 当前写 5 组（硬编码） |
| `caps_value` | ✅ | 对应 98260 `fullcap`（同为 `CAP_FULL_SET` 掩码；值相同 `0x1ffffffffff`） |
| `usage_offset` | 🔧 | fake cred usage/refcount 偏移概念；98260 当前硬编码在 offset 0，已登记 `fake_cred_usage_off` |
| `usage_value` | ❌ | 43499 写链用固定 refcount 值（256）；98260 fake usage 是自指指针 `A`，语义不同 |
| `ref0_offset` / `ref1_offset` / `ref2_offset` / `ref3_offset` / `ref_count` / `ref0_image`…`ref3_image` | ❌ | 43499 写链引用的对象/镜像；98260 fake cred 直接指向 `root_user`/`init_user_ns`/`init_ucounts`/`init_groups` |

## 6. `kernelsnitch`

| 项 | 分类 | 说明 |
|---|---|---|
| `collisions` / `mm_struct_sz` | ❌（对 98260 偏移声明） | 98260 不用 kernelsnitch；若未来共享 kernelsnitch 服务，应由**公共 service/ABI** 提供，不是 98260 参数 |

## 7. `injection` / `generator.*`（43499 写链与路由）

| 组 | 项 | 分类 |
|---|---|---|
| `injection` | `primitive`(multicast/pselect/tcp)、`victim`(process/common) | `primitive`=❌；`victim=common` 的**公用 victim 概念**=✅（98260 已用公用 victim） |
| `generator.address_model` | `direct_map_base` / `kernel_phys_load` / `kernel_phys_offset` | 🔧 线性映射基址概念 ↔ 98260 `base_min/base_max`；值/算法不复用 |
| `generator.payload` | `lock_off` / `w0_off` / `right_off` / `left_off` / `fake_task_off` / `skb_data_delta` / `skb_frag_bias` / `tcp_payload_delta` / `tcp_chunk_bias` / `tcp_fake_task_off` / `compact_waiter` / `fake_task_usage_off` / `fake_task_prio` / `fake_waiter_prio` / `write_value_offset` / `page_accept_bit` | ❌ 43499 skb/compact-waiter 写 payload 布局 |
| `generator.multicast` | `frame_size` / `family_offset` / `stamp_family` / `disarm_required` / `futex_waiter_flag` | ❌ |
| `generator.pselect` | `nfds` / `max_nfds` / `operation_budget` / `timeout_nanos` / `per_attempt_delay_ns` / `enter_delay_ns` / `fd_reserve_stride` / `select_use_timeval` / `waiter_shift` | ❌ |
| `generator.tcp` | `mapping_size` / `max_mapping` / `frame_size` / `max_frame` / `receive_size` / `operation_budget` / `timeout_nanos` / `punch_spin` / `punch_phase_spin` / `post_hold_iterations` / `arm_sequence` / `zc_address_offset` / `zc_length_offset` / `zc_task_offset` / `zc_lock_offset` / `send_fill_byte` / `mapping_fill_byte` | ❌ |

## 8. 执行策略/收尾/perf

| 项 | 分类 | 说明 |
|---|---|---|
| `failStop.enabled` | ❌ | 43499 G01 fail-stop；98260 用 park + `must_reboot` |
| `execution.owner_cpu` / `waiter_cpu` / `consumer_cpu` | 🔧 | CPU pin 概念可复用；98260 只有单 `cpu`（触发/exec/killer 同核），语义不同 |
| `writeChain.fast_repair_prebuild_enabled` / `allow_full_fallback` | ❌ | 43499 W2 repair |
| `finish.pre_dispatch_settle_ms` | 🔧 | 通用 finish 派发 settle 概念；98260 finish 在 rooted victim 内同步执行，不需要该值 |
| `perf.*`（enabled/attr/exclude/auto_enable/ring_pages/pump_*/max_passes/min_votes_*/task_alignment/register_count/sp_register_index/candidate_capacity/require_*/settle_after_acquire_us） | ❌ | 98260 不用 perf；全部 43499 专用 |

## 9. 98260 当前 profile 与 43499 旧项的逐项对照

| 98260 profile 字段 | 43499 可参考项 | 复用结论 |
|---|---|---|
| `schema_version` / `release` / `kernel_major` | 同名项 | ✅ 同义，值重填 |
| `page_size` | 无 | 98260 特有 |
| `cpu` | `execution.consumer_cpu` | 🔧 概念，值语义不同 |
| `sockbuf` / `slab_order` | 无 | 98260 特有（UAF 回填） |
| `slide_max` | 无（43499 slide 锚点非上界） | 98260 特有 |
| `base_min` / `base_max` | `generator.address_model.direct_map_base` | 🔧 概念映射 |
| `static_timer_site` / `timer_site_low21` / `copy_process_low21` | 无（43499 用 slide_* 锚点） | 98260 特有 |
| `timer_alloc` | 无 | 98260 特有 |
| `cred_req` / `cred_alloc` | `cred.copy_size` | ✅ 字段概念，值重填 |
| `cred_uid_off` | 无（43499 无 uid 偏移项） | 98260 特有 |
| `off_task_cred` | `task_struct.cred` | ✅ 公共 ABI |
| `task_alloc_bytes` | 无（43499 用 `task_struct` 字段偏移） | 98260 特有 |
| `fullcap` | `cred.caps_value` | ✅ 值同义（CAP_FULL_SET） |
| `selinux_state_static` | `offset.selinux_enforcing` | 🔧 同属 SELinux，对象/语义不同 |
| `selinux_blob_off` | `offset.selinux_blob_sizes` | 🔧 概念映射 |
| `panic_on_warn_static` | 无 | 98260 特有 |
| `root_user_static` / `init_user_ns_static` / `init_ucounts_static` / `init_groups_static` | `offset.init_cred` / `init_task` / `root_task_group` | 🔧 静态符号概念，符号不同 |
| `selinux_sid_kernel` | 无 | 98260 特有 |
| `manager_package` / `kmi` / `late_load_args` / `ksud_fallbacks` | 无（43499 finish 走 UI finish 配置） | 98260 特有 |

## 10. 需要从 43499 借鉴并提升为 98260 参数（当前硬编码）

以下 fake-cred 布局偏移目前硬编码在 `timer.c`，已作为 `BackendSpec.parameterSchema` 的 `COMPILE_CONSTANT` 项声明（`required=false`，待迁入 profile）：

- `fake_cred_usage_off`（当前 0x00）
- `fake_cred_caps_off`（当前 0x30）
- `fake_cred_security_off`（当前 0x80）
- `fake_cred_user_off`（当前 0x88）
- `fake_cred_user_ns_off`（当前 0x90）
- `fake_cred_ucounts_off`（当前 0x98）
- `fake_cred_group_info_off`（当前 0xa0）
- 另有 `uid=0x8`、`suid/euid/fsuid/securebits` 间隔、cred size 0xb8 已在 profile（`cred_uid_off`/`cred_req`）。

## 11. 已落地的 BackendSpec 参数声明

- 新增 `BackendParameterSchema` / `BackendParameterDecl`：`name/kind/scope/source/required/description`；
  - `kind`: U64/ADDRESS/OFFSET/SIZE/MASK/CPU/STRING/TOKEN_LIST；
  - `scope`: COMMON_KERNEL_ABI / BACKEND_PROFILE / BACKEND_TUNING；
  - `source`: PROFILE / COMPILE_CONSTANT / RUNTIME_DISCOVERED。
- `BackendSpec.parameterSchema` 默认空；98260 已声明 **30 个 profile 项 + 7 个待提升 fake-cred 偏移**；43499/43284 暂为空（旧 conf 不迁入；按本文分类待未来按需声明）。
- 动态 Lua spec 未来可由脚本元数据生成同一 schema（声明 ANY/SHELL 需求 + 自己的偏移项）。
- 测试：`BackendParameterSchemaTest`（无重复、common ABI 标注、待提升项 source=COMPILE_CONSTANT/required=false、内置未声明为空）。

## 12. 结论

- 可直接给 98260 用的旧内容：`release/schema_version/kernel_major`、`task_struct.cred`、`cred.caps_offset/copy_size/caps_count/caps_value`、`injection.victim=common` 概念。
- 不可直接用的旧内容：route/generator/payload layout/multicast/pselect/tcp、perf、failStop、writeChain、execution pin 值、slide 锚点、PI/ref image/seccomp。
- 98260 真正缺的是 **fake-cred 布局偏移参数化**（当前硬编码 7 项）与公共 ABI 声明复用；数值必须由 PLZ110 profile 提供。
