> 恢复说明（2026-10-06）：原始路径 ��删除提交 ����；恢复来源 git show c50e3ef44735abd0ac4c54315c80063dea1dc39f^:docs/analysis/wire-v2-kotlin-handoff.md；内容为删除前原文，未改动。

# Wire v2 Kotlin 交接（2026-09-27）

> **状态更新（接手会话已完成 B3）**：Kotlin 侧 wire v2 已落地，见计划文档
> `wire-v2-object-sections-plan.md` 的进度勾选。验证：`./gradlew :app:testDebugUnitTest
> :profile-core:test` 全绿、`make -C src native-host-tests` 全绿、golden 重冻。
> `safe_mode` 由新增的 `NativeProfileDocument.patchSafeMode()` 处理（v2 无固定槽）。
> 仅剩 B4 真机门禁需设备执行。

> 交接对象：接手完成 **Kotlin 侧 wire v2** 的 agent 会话。
> 目标：把 Kotlin 传输从旧 v3（68 定长 core + 具名 route/option）改为 **wire v2（对象分段）**，
> 与 native 已落地的 v2 严格互通。**权威格式是 `src/core/profile/binary.cpp`。**

## 0. 当前状态（同一分支）

- 分支 `exp/multicast-2`，**工作树有大量未提交改动**（native wire v2 + 早前 native v1 legacy 删除 +
  AGENTS/文档）。建议接手时先 `git status`/`git diff` 看清，必要时先把 native 半提交，再动 Kotlin。
- 计划文档：`docs/analysis/wire-v2-object-sections-plan.md`（本交接是其执行补充）。
- 相关 AGENTS 新规：**不要新增/叠加传输格式版本号，本分支统一 v2**；v1 只在 Kotlin 转 v2，
  native 不再解析 v1（`src/core/legacy/` 已删）。

## 1. 已完成（native，host tests 全绿、NDK 零告警）

- `profile/model.h`：拆成 `ProfileMeta / TaskStructOffsets / CredTemplate / KernelOffsets /
  KernelMisc(optional) / RouteGeometry(optional) / execution_settings`；`MulticastWaiterLayout /
  SelectStackLayout / TcpZerocopyLayout` 字段 `std::optional`。
- `profile/binary.cpp`：只解析/序列化 v2；`kSections[]` 定义 section→字段表；presence=键出现；
  值 u64 位严格；只写/读当前 route 的 section；未知 section/键忽略；重复键 last-wins。
- 迁移：`util.cpp`、`address_space.cpp`、`kernel/runtime_struct_offsets.h`、
  `route/select_stack_route.cpp`、`backend/*`；重写 `tests/profile_test.cpp`、
  `tests/profile_binary_test.cpp`。删除 `src/core/legacy/*`、`tests/offsets_json_test.cpp`。

## 2. wire v2 格式（权威，逐字节复刻）

```
u32 magic = 0x0D000721
u16 version = 2
u16 frontend = 1
u16 backend  = 1
u16 middleware = route wire value (1 tcp / 2 select / 3 multicast)
u16 release_len
u16 reserved = 0            # header 固定 16 字节
release[release_len]
u16 section_count
repeat section_count:
  u8 name_len, name[name_len]
  u32 entry_count
  repeat entry_count:
    u8 key_len, key[key_len]
    u64 value (LE)          # 有符号=二补数，无符号=原样
```

规则：presence 由键是否出现表达（缺省≠0）；只写/接受**当前 route** 的 `route.*` section；
未知 section/键忽略；重复键 last-wins；值按位严格（不 clamp）。

## 3. section/key 契约（必须逐字一致）

| section | 键 |
|---|---|
| `meta` | `kernel_major` `recommend_shizuku` `fallback_route` `safe_mode` |
| `task_struct` | `prio` `normal_prio` `sched_task_group` `pi_lock` `pi_waiters` `pi_top_task` `pi_blocked_on` `pid` `tgid` `atomic_flags` `real_cred` `cred` `comm` `tasks` `seccomp` |
| `cred` | `copy_size` `usage_offset` `usage_value` `caps_offset` `caps_count` `caps_value` `ref_count` `ref0_offset` `ref1_offset` `ref2_offset` `ref3_offset` `ref0_image` `ref1_image` `ref2_image` `ref3_image` |
| `offset` | `init_task` `init_cred` `empty_zero_page` `root_task_group` `selinux_enforcing` `selinux_blob_sizes` `security_hook_heads` `slide_nfulnl_logger` `slide_loggers_0_1` `slide_boot_id` |
| `kernel` | `kernel_phys_load`* `compact_waiter`* `kernelsnitch_collisions`* `mm_struct_sz`* |
| `execution.recommended_cpus` | `main` `consumer` |
| `execution.heap` | `prepare_max_attempts` `prepare_timeout_ms` `kernelsnitch_timeout_ms` |
| `execution.race` | `route_wait_ms` `route_done_timeout_ms` `setup_settle_us` `state_poll_interval_us` |
| `execution.stages` | `w1_attempts` `w1_settle_us` `w1_scratch_repair_attempts` `w2_attempts` `w2_settle_us` `w3_chain_rounds` `w3_attempts` `w3_settle_us` |
| `execution.handoff` | `pre_dispatch_settle_ms` `module_poll_attempts` `module_poll_interval_ms` `enforce_poll_attempts` `enforce_poll_interval_ms` |
| `execution.consumer` | `max_calls` `burst_calls` |
| `route.tcp_zerocopy` | `attempts` `arm_sequence` `post_receive_hold_iterations` |
| `route.select_stack` | `waiter_shift`* `compact_waiter`* `enter_delay_us` `timeout_us` |
| `route.multicast_waiter` | `waiter_off`* `buffer_size`* `task_offset`* `lock_offset`* |

`*` = optional（可缺席；native 为 `std::optional`）。

**要点：route section 用短键名**（section 名已表达 route）。现有 Kotlin route config 用的是
`mcast_waiter_off`/`pselect_waiter_shift`/`tcp_attempts` 等**长键**，必须改成短键。

## 4. Kotlin 待做（精确）

### 4.1 `profile-core/.../data/NativeProfile.kt`
- 删除 `flattenCommon()`、`fromCommon()`、`toBinaryV3()`、`fromBinaryV2/V3()`、`safeModeOffset()`。
- `toBinary()` → v2：
  - 头部 16 字节（magic/version=2/frontend=1/backend=1/middleware=routeKind/release_len/reserved/release）。
  - sections：`meta`（kernel_major/recommend_shizuku/fallback_route/safe_mode）、`task_struct`、`cred`、
    `offset`、`kernel`（可选字段为 null 则省略）、`execution.*` 六组、`route.<当前 route>`。
  - 条目值用 `ULong` 位容器：`UInt→toULong()`、`Int→toLong().toULong()`、`ULong` 原样；`null` 省略。
  - 只写当前 route 的 route section。
- `fromBinary()` → v2：按 section 名 + 键解析；`route.*` 只接受与 `middleware` 匹配的 section；
  optional 缺省填 `null`；未知忽略。
- 常量：`Version = 2u`；删除 `VersionV3/HeaderSizeV3/CommonFieldCount`；`HeaderSize = 16`。

### 4.2 类型对齐 + 可空（Kotlin 与 native 同符号/宽度）
- `NativeProfileDocument`：`kernelPhysLoad: ULong?`、`compactWaiter: UByte?`、
  `kernelsnitchCollisions: UInt?`、`mmStructSz: UInt?`。
- route configs（`data/route/*.kt`）：
  - `MulticastGeometry.waiterOff: Int?`、`bufferSize/taskOffset/lockOffset: UInt?`；
    `entries()` 返回短键 `waiter_off/buffer_size/task_offset/lock_offset`，值 `ULong`，null 省略。
  - `SelectConfig`: `waiterShift: Int?`、`compactWaiter: UByte?`、`enterDelayUs: UInt?`、
    `timeoutUs: UInt?`；短键 `waiter_shift/compact_waiter/enter_delay_us/timeout_us`。
  - `TcpConfig`: 短键 `attempts/arm_sequence/post_receive_hold_iterations`（可保持非空）。
- **去掉 `toConfigUInt()/toConfigULong()` 的 clamp**；改为严格：越界/无符号负值在**校验**阶段判 invalid，
  不静默改写。

### 4.3 Resolver / 校验
- `ProfileResolver.nativeValue`：把 route 分支字段名改为短键；`mcast.*` 路径映射到
  `route.<name>.waiter_off` 等短字段；保持返回 `Long?`（null 透传）。
- `Profile.fromValueMap`（`app/.../data/Profile.kt`）与 `multicastLayout()` 等：字段改可空。
- `AndroidProfileConfigController.validateProfileFields`：optional 缺失 → invalid；
  无符号字段出现负值 / 32 位越界 → invalid（不要 clamp）。
- 合并（`deepMergeValues`）：缺席/`null` **不覆盖**已有值（补测试）。

### 4.4 测试与 golden
- `NativeProfileDocumentTest`：routeEntries 断言改为 v2 section/短键。
- `ProfileRoundTripTest`、`ControllerOverrideTest`、`OffsetMatchingTest`：按新类型/键调整。
- `app/src/test/resources/native-doc-golden.sha256`：Kotlin 输出变为 v2 后**重冻**（或改为按 release 生成）。
- 新增：presence（缺席≠0）、0/负数严格、无符号越界→invalid 的用例。

## 5. 验收命令
```
./gradlew :app:testDebugUnitTest          # 全绿（含重冻 golden）
make -C src native-host-tests             # 已绿（回归确认）
ANDROID_NDK_HOME=<ndk> make -C src ghostlock   # 零告警
```
真机：内置 5.15 冷机单 route（multicast），确认 wire 往返不影响攻击；流程见 AGENTS。

## 6. 约束
- 不改攻击路径语义；section/键名以 `binary.cpp` 为准（不要各写各的）。
- 版本统一 v2，不再起 v3/v4（AGENTS）。
- 不改 `kernelsnitch/`、不改 v1 转换（`LegacyProfileConverter.kt` 保持转 v2）。
- 提交信息风格 `type(scope): summary`；未经要求不要 push。

## 7. 已知风险
- route 短键改名是**双侧契约**：漏改一处会让 native 收不到该字段（表现为 `nullopt`→拒绝或默认），
  必须在 §3 表上逐条核对 `entries()` 与 native `kField` 键。
- `recommend_shizuku` 在 v2 放 `meta` section（native 存但运行时不用），别遗漏。
- 若 Kotlin 暂未完成，app 产出的 wire 与 native 不匹配，**不可上真机**。
