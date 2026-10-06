> 恢复说明（2026-10-06）：原始路径 ��删除提交 ����；恢复来源 git show c50e3ef44735abd0ac4c54315c80063dea1dc39f^:docs/analysis/wire-v2-object-sections-plan.md；内容为删除前原文，未改动。

# Wire v2：对象分段 + 可变 schema + 严格值 计划（2026-09-27）

> 说明：本分支统一为 v2；旧的 v2/v3 定长格式直接替换（Kotlin↔native 版本绑定，不做兼容）。
> `src/core/legacy/` 的 v1 JSON 路径已删除，v1 只由 Kotlin `LegacyProfileConverter.kt` 转 v2。

## 现状与基线

- 分支 `exp/multicast-2`，基线提交 `1b45387`。
- 当前 wire = GLK1 **v3**（`profile-core/.../NativeProfile.kt::toBinaryV3/fromBinaryV3`）：
  `header(16) + release + 68×u64 定长 common + u16 middleware_count + [u8 len,key,u64] +
   u16 option_count + [u8 len,key,u64]`。
- `common` 是**位置编码**的 68 个 slot（`flattenCommon()` ↔ native `binary.cpp::kCommonFields[]`），
  顺序强耦合；增删字段必须两侧同步改索引。
- route/option 段已是“名字→u64”，但只在少数字段使用；native 解码后**无法区分“键缺失”和“值 0”**。
- `MulticastConfig` 等对无符号字段用 `toConfigUInt()/toConfigULong()` **clamp**（负数→0），与
  “严格保留 0/负数”冲突。
- Kotlin 侧曾把“核心 profile 对象”和“一般执行参数”在模型上拆分（TaskStructOffsets / CredTemplate /
  KernelOffsetTable / ExecutionTuning），但 wire 仍混在 68 slot 里，**未做对象级对应**。

## 目标与约束

### 目标

1. wire 改为**对象分段**：每个模型对象一个具名 section，section 内是“字段名→u64”条目，长度可变。
2. **presence 与值正交**：字段“提供/未提供”= 条目是否出现；值按 u64 位型**严格保存**（0/负数/正数原样），
   不做 clamp。
3. native 结构体**拆分为与 Kotlin 对象一一对应的子结构**，可选字段用 `std::optional` / presence。
4. **去掉定长 68**，schema 可增删字段而不改容器（可扩展）。
5. Kotlin 对象字段相应改为可空（`Long?`/`Int?`），`entries()` 只输出非 null；渲染 `= null`。
6. **数值符号/宽度双侧对齐**：Kotlin 字段类型必须与 native 成员的符号和宽度一致——
   `uint8_t→UByte`、`uint32_t→UInt`、`uint64_t→ULong`、`int32_t→Int`、`int64_t→Long`；
   可选字段加 `?`（如 `std::optional<uint32_t>` ↔ `UInt?`）。不再出现 Kotlin 用 `Long` 承载
   native `uint32_t`、或 `toConfigUInt` 之类改写的情况。wire 的 u64 仅作为位容器。

### 非目标

- 不改 HOCON 配置文件格式与 extractor 输出（extractor 只产 HOCON；wire 由 app 生成）。
- 不改路由算法/攻击代码语义（仅数据搬运）。
- 不保留 v2/v3 解码（native 与 Kotlin 版本绑定，可按用户要求直接替换 v3）。
- 不改 `legacy/`（v1 offsets.json）。

## 设计

### D1：wire v2 布局

```
u32 magic
u16 version = 4
u16 frontend
u16 backend
u16 route_kind
u16 release_len
release[release_len]
u16 section_count
repeat section_count:
    u8  name_len, name[name_len]            # "task_struct", "cred", "offset",
                                            # "kernel", "execution.<group>",
                                            # "route.multicast_waiter", "meta"...
    u32 entry_count
    repeat entry_count:
        u8  key_len, key[key_len]
        u64 value                            # 原始位型（有符号二补数）
```

- 无定长 core；无位置耦合；section/entry 顺序无关（解析按名字）。
- 扩展：新增字段 = 在该 section 加一个条目；新增对象 = 加一个 section。
- presence：条目出现即“提供”；`u64` 直接位拷贝，`0`/负数合法。
- `meta` section 承载：`kernel_major`、`recommend_shizuku`、`fallback_route`、`safe_mode` 等标量。

### D2：对象级对应

| wire section | Kotlin 对象 | native 结构体 |
|---|---|---|
| `task_struct` | `TaskStructOffsets` | `TaskStructOffsets`（拆自 kernel_offsets） |
| `cred` | `CredTemplate` | `CredTemplate` |
| `offset` | `KernelOffsetTable` | `KernelOffsets` |
| `kernel` | misc（phys_load/compact_waiter/kernelsnitch/mm_struct_sz） | `KernelMisc`（optional 字段） |
| `execution.<group>` | `ExecutionTuning`（cpus/heap/race/stages/handoff/routes/selected_cpus） | `execution_settings` 的对应子结构 |
| `route.<kind>` | `MulticastConfig`/`SelectConfig`/`TcpConfig` | 各 route layout（optional 字段） |
| `meta` | `NativeProfileDocument` 头部标量 | `ProfileMeta` |

- 每个对象的“可选/可自动回退”字段在 wire 里可缺席；native 侧为 `std::optional<T>`，Kotlin 侧为
  `T?`；缺席 = 不提供，值 = 精确。
- 使用点：`MulticastWaiterLayout.waiter_offset` 改 `std::optional<int32_t>`；one-shot route 遇
  `nullopt` 直接拒绝（不写入），与 Kotlin 校验（缺失→invalid）双保险。

### D3：native 模型拆分

- `profile::kernel_offsets` 拆为：
  `ProfileMeta`（kernel_major/recommend_shizuku/route/fallback_route/safe_mode）、`TaskStructOffsets`、
  `CredTemplate`、`KernelOffsets`、`KernelMisc`、`ExecutionSettings`；顶层 `TargetProfile` 持有这些对象。
- 访问器/使用点（`address_space.cpp`、`route/*`、`backend/*`、`attack/*`）机械迁移到子对象。
- `binary.cpp` 字段表按对象分组（对象名 + 字段表）；解码按 section 名分派，条目缺失→`nullopt`。
- **严格值**：`to_raw/from_raw` 已有；去掉所有 clamp，签名/位型原样搬运。

### D4：Kotlin 侧

- `NativeProfile.kt`：`toBinaryV2/fromBinaryV2`；对象 → sections；可选字段 nullable；`entries()` 省略 null。
- `MulticastConfig`（及 Select/Tcp）字段改 nullable；移除 `toConfigUInt/ULong` clamp（值原样 Long）。
- `ProfileResolver.nativeValue` 已返回 `Long?`；`Profile.fromValueMap` 不再把 null 归零（保留 null 语义）。
- 校验：`waiter_off` 等缺失 → `invalid` → 阻止运行；提供则严格使用（含 0/负数，语义合法性另判）。
- 合并：`deepMergeValues` 对 `null` 跳过（不覆盖）——补测试钉死“缺席不覆盖已有值”。

### D6：数值符号/宽度对齐与严格范围

- Kotlin 字段类型严格等于 native 成员类型（目标 6），可选字段加 `?`；不再用宽类型承载或
  `toConfigUInt()` 改写。
- wire 条目值统一为 **`ULong`（u64 位容器）**：`UInt → toULong()`、`Int → toLong().toULong()`
  （符号扩展二补数）、`ULong` 原样；native `to_raw()` 对无符号 `static_cast<uint64_t>`、对有符号
  符号扩展，位型一致。
- **严格性**：不做 clamp/截断。用**范围校验**代替：无符号字段出现负值、或值超出字段宽度
  （如 32 位字段 > `0xFFFFFFFF`）→ **invalid**（拒绝运行），而不是静默 wrap/归零。
- native 解码：`from_raw<T>()` 原样按位；若 Kotlin 已按类型校验，native 端保持一致（越界视为无效，
  与 Kotlin 双保险）。
- 测试：每种类型的往返（`0` / 最大值 / 负值）、越界→invalid、presence（缺席≠0）。

### D5：数据流/控制流差异

```mermaid
flowchart LR
  subgraph V3[wire v3]
    A1[header + 68 定长 core + 具名 route/opt] --> A2[native 位置解码, 缺失=0]
  end
  subgraph V2[wire v2]
    B1[header + sections: 对象名 → 字段名 → u64] --> B2[native 按名解码, 缺失=nullopt]
  end
```

不变量：HOCON/extractor 不变；route/攻击语义不变；值位型严格。

## 改动清单

| 文件 | 改动 | 理由 |
|---|---|---|
| `profile-core/.../NativeProfile.kt` | v2 编解码；对象→section；nullable；条目值 `ULong` | D1/D2/D4/D6 |
| `profile-core/.../route/*Config.kt` | 字段类型对齐 native（UInt/ULong/Int）+ nullable；去 clamp | D4/D6 |
| `profile-core/.../ProfileResolver.kt`/`ValueModel.kt` | null 语义、合并不覆盖 | D4 |
| `app/.../AndroidProfileConfigController.kt` | 校验：缺失→invalid；渲染 null | D4 |
| `src/core/profile/model.h` | 拆子结构 + optional | D2/D3 |
| `src/core/profile/binary.cpp` | v2 section 解码；对象字段表 | D1–D3 |
| `src/core/memory/address_space.cpp`、`route/*`、`session/*`、`attack/*` | 访问器迁移 | D3 |
| 测试（profile_binary/profile/offsets_json/host） | v2 往返、presence、0/负数严格、缺席不覆盖 | 验证 |
| 文档（PROFILE_SCHEMA、defaults、src/core/README） | 记录 v2 | 规范 |

## 验证矩阵

| 批次 | 检查 | 预期 |
|---|---|---|
| B1 wire 容器+对象往返 | `profile_binary_test`（v2：对象往返、缺字段=nullopt、0/负 严格、符号/宽度对齐、越界=invalid） | 通过 |
| B2 native 模型拆分 | `make -C src native-host-tests`、NDK 零告警、`lint-tidy` 0 | 通过 |
| B2 | `cmp_disasm`（若触及攻击函数内联则复核；profile 解析不在 8 函数内） | IDENTICAL/复核 |
| B3 Kotlin | `./gradlew :app:testDebugUnitTest`（含 golden 重冻） | 通过 |
| B4 真机 | 内置 5.15 冷机单 route | W1/W2/W3 通过（wire 往返不影响攻击） |

## 明确保留

- HOCON 配置格式、extractor 输出、`legacy/` v1、route/攻击代码语义、`g_exploit_session`/`g_direct_map_end`。
- 8 攻击函数机器码（wire 改动不进入攻击路径）。

## 进度

- [x] Explore：wire v3 布局、68 slot、clamp、对象划分现状。
- [x] Design：本计划；native v1 已删、AGENTS 加版本纪律。
- [x] B1/B2 native：wire v2 容器 + 对象 section 表 + 模型拆分 + presence/严格值/类型对齐；
  迁移 util/address_space/runtime_struct_offsets/select_stack_route/backend；重写 profile/profile_binary 测试。
  验证：`make -C src native-host-tests` 全绿、NDK 零告警。
- [x] B3 Kotlin：`NativeProfile.kt` v2 编解码 + route configs 短键/可空/去 clamp + resolver/校验 + golden。
  验证：`./gradlew :app:testDebugUnitTest :profile-core:test` 全绿、`make -C src native-host-tests` 全绿、golden 重冻。
  实现要点：`toBinary()` 改为 v2 对象分段；optional 字段可空（`kernelPhysLoad`/`compactWaiter`/
  `kernelsnitchCollisions`/`mmStructSz`）；route section 用短键（`waiter_off`/`waiter_shift`/`attempts`…）；
  新增 `NativeProfileDocument.patchSafeMode()` 取代固定偏移 `safeModeOffset()`（v2 无固定槽）；
  controller 校验对无符号字段越界/负值判 invalid（不再 clamp）。
- [ ] B4：真机回归（需设备）。
