# Native 组件架构 Batch 2 设计：版本化 Kotlin/native 组件 DTO（2026-09-23）

> 本文件细化 `docs/analysis/native-component-architecture-plan.md` 的「Batch 2」。
> 依赖 Batch 1（`:profile-core`，提交 `ad264c8`）。冲突时以总计划为准并回写本文件。

## 现状与基线

- 分支 `very-not-stable-dev`；Batch 1 提交 `ad264c8`；代码基线 `b411bb4`。
- Batch 1 已把无 Android 依赖的值模型/解析/合并/序列化集中到 `:profile-core`，并锁定 51 个
  设备 release 的 v2 字节 golden（`app/src/test/resources/native-doc-golden.sha256`）。
- native transport 现状（v2，`src/core/profile/binary.cpp` / `binary.h`）：
  - header 12 字节：`u32 magic(0x0D000721) + u16 version(2) + u8 route + u8 kernel_major +
    u8 recommend_shizuku + u8 fallback_route + u16 release_length` + release；
  - 固定 68 个 `u64` 公共槽（`kCommonFields`：task/cred/offset/misc/execution/safe_mode）；
  - per-route 段：`u8 count` + N×(`u8 key_len` + key + `u64`)；三张表 `kTcpFields`/`kSelectFields`/
    `kMulticastFields`；
  - `kRouteAuto` 被拒绝；未知 key 被忽略；未知 version/短包被拒绝。
- native 执行结构是 `profile::kernel_offsets`（`model.h`），由 `TargetProfile` 包装为只读快照；
  route 生命周期与 procedure 读取 `TargetProfile` 的 accessor。
- Kotlin 侧 `NativeProfileDocument`（`:profile-core`）是 v2 的逐字段镜像；Batch 1 的 exporter 与 App
  现在共用它。
- Batch 0 结论：v2 wire 与 HOCON schema 是 dev 专有，**无二进制兼容义务**，可直接替换。

## 目标与范围

### 本批目标（总计划 Batch 2）

1. 把 Kotlin 的 `NativeProfileDocument` 拆成 **core DTO / component DTO / runtime options / wire codec**；
   codec 不再承载"配置来自哪一层"的信息。
2. `src/core/profile/model.h`、`binary.h`、`binary.cpp`：定义 native 对应的只读 core/component/options
   DTO 与**新版本解码适配**。
3. 逐字段校验类型、signedness、默认值、未知字段/版本行为与 round-trip；Kotlin/native 字段表**单一
   定义来源**，不允许各自手维护。
4. wire 版本与二进制字段表只在本批变更。

### 明确非目标

- 不接 Orchestrator、不做组件兼容性分派（Batch 3）；不改 route 算法、时序、payload、内存布局。
- 不改 `kernelsnitch/`、legacy v1 converter、Shizuku/KSU 路径。
- 不引入 UMH frontend / CVE-2026-64560 backend（Batch 4/5）。
- 不把 wire 升级作为执行行为调优的机会；字段数值语义保持不变。

## 关键约束

- **执行路径不动**：`TargetProfile` 的 accessor 与 `route/*`、procedure 的读取点在本批不改语义。
  新 DTO 解码后应适配回 `kernel_offsets`（或其超集），使执行代码无感。
- **Kotlin/native 单一字段表**：Batch 1 后 Kotlin 侧唯一定义在 `:profile-core`；本批要让 native 的表与
  它由**同一份清单**校验（cross-language fixture），而不是两份手写表。
- 不新增可变 native 全局；不在 PI 竞争窗口增加间接调用。
- wire 版本升级必须 fail closed：未知 version、未知必需能力、截断一律拒绝。

## 决策（已定，2026-09-23，用户确认「按推荐执行」）

- **D1 = B（渐进组件化）**：wire v3 = core 段（沿用 v2 的 68 固定槽）+ middleware 段（沿用现有
  per-route 键值表）+ options 段（safe_mode、selected_cpus）；selection 写
  `root_child` / `cve_2026_43499` / 现有 middleware ID；native 解码后适配回 `kernel_offsets`。
- **D2 = 保留 v2 reader**：native 同时接受 v2/v3，v3 为 writer 默认；v2 移除留到 Batch 6。
- **D3 = `u16` 枚举**：frontend/backend/middleware ID 各 `u16`，显式数值，不序列化编译器布局。
- **D4 = 调优归属**：通用 execution 与 route 私有 tuning 的落点见下表；`selected_cpus` 属 options。

| 语义 | v2 位置 | v3 位置 |
|---|---|---|
| task/cred/offset/kernelsnitch/misc | 68 公共槽 | core 段（同 68 槽） |
| 通用 execution（w1/w2/race/handoff…） | 公共槽 | core 段（同一组槽） |
| `recommend_shizuku` | header `u8` | **移除**（App-only；适配时置 0，执行路径不读它） |
| route 私有 tuning（tcp/select/multicast） | route 段 | middleware 段 |
| `component selection` | 单一 route `u8` | frontend/backend/middleware 各 `u16` |
| `safe_mode` / `selected_cpus` | 公共槽 / 折叠进 recommended | options 段 |
| `fallback_route` | header `u8` | header `u8`（保留） |

## wire v3 形状（草案，按 D1=B）

```
u32 magic(0x0D000721)
u16 version(3)
u8  frontend_id      // root_child
u8  backend_id       // cve_2026_43499
u8  middleware_id    // tcp_zerocopy | select_stack | multicast_waiter
u8  kernel_major
u16 release_length
release bytes (UTF-8)
core section:        u16 field_count + N×(u8 key_len + key + u64)   // 原 common 槽
middleware section:  u16 field_count + N×(u8 key_len + key + u64)   // 原 route 段
options section:     u16 field_count + N×(u8 key_len + key + u64)   // safe_mode, selected_cpus…
```

- 用"命名段 + 键值"而非固定槽，便于 Batch 3-5 增组件而不改 header。
- 未知 section/必需键缺失 → 拒绝；未知键 → 按 D2/严格度决定（建议拒绝并报路径，与 Kotlin
  `ProfileResolver.validateMerged` 一致）。
- header 的 `recommend_shizuku` 不再进入 wire（Batch 1 已把它归 AppPolicy）；run 决策不依赖它。

## Kotlin 类型拆分（示意）

```kotlin
data class CoreDto(val release: String, val kernelMajor: UInt, val fields: Map<String, Long>)
data class MiddlewareDto(val kind: MiddlewareId, val fields: Map<String, Long>)
data class RuntimeOptionsDto(val safeMode: Boolean, val selectedCpus: Map<String, Long>)
data class ComponentSelectionDto(val frontend: FrontendId, val backend: BackendId, val middleware: MiddlewareId)
data class RuntimeDto(val core, val selection, val middleware, val options)
object RuntimeCodec { fun encode(RuntimeDto): ByteArray; fun decode(ByteArray): Result<RuntimeDto> }
```

实际实现应复用 Batch 1 的 typed 模型（`CoreProfile` 等）并避免第二份字段表。

## 数据流/控制流差异

```text
现状（v2）：ProfileMerger -> NativeProfileDocument(v2) -> App/exporter -> native binary::parse -> kernel_offsets
目标（v3）：ProfileMerger -> RuntimeDto -> RuntimeCodec(v3) -> native runtime_codec -> RuntimeDto(native) -> kernel_offsets(适配)
```

不变量：

1. 执行路径读取的值与 v2 语义逐字段一致（由 golden 迁移验证）。
2. 未知 version/必需键 fail closed，可诊断。
3. Kotlin/native 字段表来自同一清单，测试锁定。
4. wire 升级不改变 route 生命周期、资源所有权与清理顺序。

## 影响文件（初步）

| 文件 | 改动 |
|---|---|
| `profile-core/.../profile/RuntimeDto.kt` | 新增 core/middleware/options/selection DTO |
| `profile-core/.../profile/RuntimeCodec.kt` | 新增 v3 encode/decode；v2 兼容（按 D2） |
| `profile-core/.../NativeProfile.kt` | 逐步退化为 v2 reader 或删除（按 D2） |
| `src/core/profile/binary.h`、`binary.cpp` | 新增 v3 reader/writer；保留 v2 reader（按 D2） |
| `src/core/profile/model.h` | 新增只读 runtime/core/component DTO + 适配到 `kernel_offsets` |
| `src/core/profile/entry.cpp` | 选择 v3/v2 解码并产出 `TargetProfile` |
| `src/core/tests/profile_binary_test.cpp`、`target_constants_test.cpp` | v3 round-trip、未知/截断拒绝、适配等价 |
| `app/src/test/**` | cross-language fixture、golden 迁移 |
| `docs/development/engineering-standards.md`、`PROFILE_SCHEMA*` | wire v3 说明 |

## 兼容性与回滚

- dev v2 无兼容义务；App 与 native 必须同批升级。v2 reader 的保留由 D2 决定，最迟 Batch 6 移除。
- 回滚单位：源码批次 + wire 版本常量 + golden。每步保留可复现构建与 host tests。
- 发现执行值差异时先查明映射，不用默认值掩盖。

## 验证矩阵

| 项 | 命令 | 预期 |
|---|---|---|
| native 单测 | `make -C src native-host-tests` | v3 round-trip、未知/截断拒绝、适配等价 |
| Kotlin | `./gradlew :app:testDebugUnitTest :profile-core:test` | codec、cross-language fixture、golden 通过 |
| 构建 | `make -C src ghostlock`、`./gradlew :app:assembleDebug` | 零告警 |
| 攻击路径 | 若触 route config 读取：`tools/cmp_disasm.py` + 真机门禁（AGENTS） | 8 函数 IDENTICAL 或已复核差异 |

profile 二进制解码本身不是 waiter/race/payload 流程，但若本批改动 `TargetProfile` accessor 或 route
config 读取点，则按攻击关键路径升级验证。

## 明确保留

- `kernelsnitch/`、legacy v1 converter、现有 CVE-2026-43499、W1/W2/W3、三种 middleware 的算法与时序。
- `TargetProfile` 的对外语义（release/route/supports/layout accessor），本批只在其下层引入 DTO。
- 不新增可变 native 全局、不引入虚基类 provider。

## 进度

- [x] 只读调查 native profile/transport 现状（`binary.cpp`/`model.h`/`binary.h`/`entry.cpp`）。
- [x] 产出本 Batch 2 设计；D1=B、D2 保留 v2、D3 `u16`、D4 归属（用户确认）。
- [x] native：`binary.cpp` 新增 v3 reader/writer（header 16 + core 68 槽 + middleware 段 +
  options 段），`parse` 按 version 分派 v2/v3；`binary.h` 常量与注释更新。
- [x] Kotlin：`NativeProfileDocument.toBinaryV3()` 与 `fromBinary` v2/v3 分派；App/exporter 改输出 v3。
- [x] 测试：`profile_binary_test` v3 round-trip 与 v2 兼容；v3 golden 重建；全量 Gradle 测试通过。
- [x] 验证：`make -C src native-host-tests`、`make -B -C src ghostlock`（零告警）、
  `make -C src lint-tidy`（0 findings）、`./gradlew :app:testDebugUnitTest :profile-core:test exportKernelProfiles`。

### 实现偏差（与本文设计相比）

- v3 的 core 段沿用 v2 的 **68 个固定槽**（而非纯键值），middleware/options 段用键值；这样 native 复用
  现有 `kCommonFields` / route 表，执行值等价直接可得。
- header 用 `u16 frontend/backend/middleware` + `u8 kernel_major + u8 fallback_route`，并**移除**
  `recommend_shizuku`（适配时置 0；执行路径不读它）。
- Kotlin 不新增独立 `RuntimeDto`/`RuntimeCodec` 文件，而是给 `NativeProfileDocument` 增加 `toBinaryV3()`
  与 decoder 分派；typed 拆分（core/component/options 类）留待 Batch 3 随 Orchestrator 落地。
- `NativeProfileDocumentTest` 仍测 v2 codec；v3 语义由 native `profile_binary_test` 与 v3 golden 覆盖。
