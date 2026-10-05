# Kernel Profile 结构文档（配置系统）

> English: [PROFILE_SCHEMA.md](PROFILE_SCHEMA.md)

本文档是 `app/src/main/assets/kernel_profiles/` 下内核配置（profile）的参考：HOCON
布局、字段分组、组合 token 选择、插件段、GLKv3 wire，以及加载/校验流水线。

> 适配新内核的操作步骤见 [README_ZH.md](README_ZH.md)；`execution` 调优默认值见
> [defaults_ZH.md](defaults_ZH.md)。**本文不重复画结构图**：全流程结构（IPO / 状态机 /
> Class / Sequence）的唯一权威是
> [full-process-uml.md](../development/full-process-uml.md)。

## 0. 版本与字段权威

版本号**只有一个**：`3`。HOCON 配置写 `ghostlock.schema_version = 3`，wire 文档是
MessagePack 根 map、其 `schema` 必须等于 `3`；不存在按文件叠加/递增的版本。插件 C ABI
是**另一个**计数器：`GLK_ABI_VERSION = 1`（仅尾部追加，
`src/core/contract/abi/glk_contract_abi.h`）。

| 事实 | 权威 |
|---|---|
| canonical profile 形状、键归属、别名归一 | 仓库内 `app/src/main/assets/kernel_profiles/*.conf`；解析器 `profile-core/.../data/ProfileLayout.kt` |
| owner-qualified path → wire 类型（109 字段） | native 导出 `app/src/test/resources/profile-manifest-v3.tsv`（对拍副本）与 `profile-core/src/main/resources/profile-manifest-v3.tsv`（运行时副本）；生成命令 `make -C src profile-manifest-v3` |
| 选择词汇（12 个组合 token） | `contract::kCombinationCatalog`（`src/core/contract/identity.hpp`）；导出 `make -C src combination-manifest` |
| 插件 wire 形状与动态键 | `src/core/plugin/schema.hpp`（`kPluginGlkv3Fields`，line 95）与 `src/core/plugin/wire.{hpp,cpp}`（`validate_plugin_wire`，`wire.hpp:66`） |
| 插件 C ABI 版本 | `src/core/contract/abi/glk_contract_abi.h:47`（`GLK_ABI_VERSION 1u`） |
| token 目录 | `src/core/contract/identity.hpp:177`（`kCombinationCatalog`，12 行） |
| 运行索引 | `app/src/main/assets/kernel_profiles/index.conf:3`（`schema_version = 3`） |
| GLKv3 wire 格式 | [wire-transport-model.md](../analysis/wire-transport-model.md) |
| 全流程结构图 | [full-process-uml.md](../development/full-process-uml.md)（唯一权威；本文只链接，不重画） |

## 1. 数据流

1. **加载**：设备精确 `uname -r` 对应的内置 profile、共享 `execution-*.conf` preset、
   导入/导出的用户 profile 与高级覆盖；片段通过 `include` 引入。
2. **归一**：所有文档统一成 canonical owner-qualified 布局（`ProfileLayout`）——canonical
   与旧的扁平写法都接受，别名被解析，未识别键带点分路径 **fail-closed**。
3. **合并**（低 → 高）：execution preset → 内置 + 导入 profile → 高级覆盖；随后
   `execution.selected_cpus` 按用户选择的 CPU 对强制写入（`ProfileMerger`/`ProfileResolver`）。
4. **校验**：release 必须与设备一致；schema 必填字段必须在；默认值由 schema 物化（被默认的
   字段以 `default_used` 报告）。
5. **编码**：解析后的 profile 编码为 GLKv3 文档（根 map、`schema == 3`），canonical 编码
   （最短整数、map 键按 UTF-8 字节序排序）。
6. **分帧与交接**：App 启动 native 可执行文件，把文档按「4 字节大端长度前缀 + 文档」写入
   stdin；同一流上可再接一帧运行时密钥。密钥绝不进文档、不进 argv、不落盘。
7. **native 解析与绑定**：非 map 根、缺 `schema` 或 `schema != 3`、未知 section/键、类型
   不符、截断文档、超过 1 MiB 的文档——在任何攻击阶段之前一律拒绝。

## 2. 文件格式（HOCON）

- 全部使用 HOCON：内置 profile、`index.conf`、片段、导入与导出文件。JSON 仍然合法；
  接受 `#` / `//` 注释、尾逗号与 `${var}`（含 `${?var}`）替换。
- 支持 `include "file.conf"`（同目录、可嵌套、防循环）。**被 include 的片段不带
  `schema_version`**，只有 profile 根带。
- `index.conf` 是运行索引：

```hocon
schema_version = 3
backends = [
  { id = "cve_2026_43499", available = true }
  { id = "cve_2026_43284", available = true }
]
profiles = [
  { release = "6.12-template", file = "6.12-template.conf" }
  { release = "6.12.23-android16-5-g16e473de48a3-abogki462654244-4k", file = "6.12.23-android16-5-g16e473de48a3-abogki462654244-4k.conf" }
]
```

  `backends` 矩阵与 native 导出的 manifest 对拍（`BackendMatrixAgreementTest`），不会漂移。

## 3. canonical 布局（owner-qualified）

内置 profile 使用唯一包裹根 `ghostlock`，每个 owner 一个段。下面是一份真实 profile 的完整形状：

```hocon
# GhostLock kernel profile (HOCON, canonical R3 owner-qualified layout).
ghostlock {
  include "credential-6x.conf"
  include "kernelsnitch-6x.conf"
  schema_version = 3
  release = "6.12.23-android16-5-g16e473de48a3-abogki462654244-4k"
  selection {
    backend  = "cve_2026_43499"     # 下面 steps token 的归属 backend
    terminal = "root_child"         # 与 token 的一致性校验
  }
  common {
    kernel_major = 6
  }
  platform {
    abi {
      task_struct { prio = 148, cred = 2304, comm = 2320 /* ... */ }
      offset { init_task = 37736192, init_cred = 37825128 /* ... */ }
      kernel { kernel_phys_load = null, kernel_phys_offset = null }
    }
  }
  backend {
    cve_2026_43499 {
      steps = "pselect_rootchild"   # 唯一对用户可见的选择 token
      route { select_stack { waiter_shift = 0 } }
      offset { slide_loggers_0_1 = 37691640 /* ... */ }
    }
  }
}
```

按字段数的归属（native manifest，109 行）：

| Owner 段 | 字段数 | 承载 |
|---|---|---|
| `backend.cve_2026_43499` | 58 | steps token、route 几何、凭据模板、KernelSnitch 值、execution 调优 |
| `platform.abi` | 31 | `task_struct`、ABI 级 `offset`、`kernel_phys_*` |
| `backend.cve_2026_43284` | 10 | 页缓存/LKM 策略：模块与 carrier 路径、握手超时 |
| `plugin.<id>` | 6 | 插件段（4 条静态行 + 2 条动态行），见第 5 节 |
| `countermeasure.vivo_vr_guard` | 1 | 厂商对策参数 |
| `common` | 3 | `kernel_major`、`safe_mode`、`vr_guard` |

字段数可复核：

```sh
awk -F'\t' '!/^#/{split($2,a,"."); print a[1]"."a[2]}' app/src/test/resources/profile-manifest-v3.tsv | sort | uniq -c
```

规则：

- 一个事实一个 owner：键放在**消费它的组件**所属段；共享值放 `common`；
- `selection.backend` 与 `selection.terminal` 只是**一致性校验**——真正的选择是
  `backend.<id>.steps` 里的 token；
- 未识别的键带点分路径**拒绝**（fail-closed），不静默丢弃；
- 旧的扁平文档在**解析期仍被接受**并归一成本布局；新写的 profile 必须是 canonical 布局。

## 4. 选择：唯一组合 token

对用户可见的选择只有 **一个 token**，存放在 `backend.<id>.steps`。token 派生出 route、
step 集与 terminal；这三者不再可独立选择。

| 可用（7） | 计划（5） |
|---|---|
| `mcast_rootchild`、`pselect_rootchild`、`tcp_rootchild` | `mcast_umh`、`pselect_umh`、`tcp_umh` |
| `mcast_shizuku`、`pselect_shizuku`、`tcp_shizuku` | `rootchild`、`shizuku`（cve_2026_43284） |
| `umh`（cve_2026_43284） | |

- `_` 前的 owner 前缀就是 route：`mcast` = `multicast_waiter`、`pselect` =
  `select_stack`、`tcp` = `tcp_zerocopy`；无 route 轴的 backend（cve_2026_43284）
  用裸 path 名；
- 计划 token 可解析、被登记，但选择门禁拒绝、App 置灰；
- 未知 token 拒绝并回显 token 文本；缺 `steps` 键拒绝；
- 根 `route` / `terminal` 必须与 token 一致，否则拒绝文档；
- 旧文档里携带的数字 step id 或旧 step token 会带 stderr 诊断迁移为等价的组合 token。

## 5. 插件段（P1）

`plugin` 是第三类顶层 owner（既不属于某个 backend，也不是 platform）——同一对策可服务多个
backend：

| 路径 | 类型 | 规则 |
|---|---|---|
| `plugin.<id>.enabled` | bool | 默认 false；**只有 `true` 才会被发射** |
| `plugin.<id>.stage` | str | host stage token 之一（`pre_spawn`、`post_spawn`、`pre_terminal`、`post_terminal`） |
| `plugin.<id>.module_path` | str | 相对 `<GHOSTLOCK_HOME>/countermeasures`；不得绝对路径、不得含 `..`、不得含反斜杠 |
| `plugin.<id>.module_hash` | str | 64 位小写 hex（模块的 SHA-256） |
| `plugin.<id>.params.<key>` | 动态 | 值类型由插件描述符决定 |
| `plugin.<id>.extract.<key>` | 动态 | 由 extractor 投影产出；此处只校验形状 |

- 两条动态路径在 manifest 里以**联合类型** `uint|int|bool|str` 声明；具体键的类型由**已加载
  模块的描述符**决定（经只读 native 探针 `--plugin-probe` 读取），不是静态表；
- 文档 fail-closed：未知字段、`enabled` 缺失/非 bool/为 `false`、未知 stage、坏 module
  path 或 hash、空动态键、插件数超过 16——全部在攻击前拒绝，绝不静默丢弃；
- **没有**插件资产文件：插件配置属设备/用户特有，走覆盖存储；App 只为已启用插件写
  `plugin.<id>.*`；
- 插件 C ABI 是 `GLK_ABI_VERSION = 1`、仅尾部追加；探针输出 TSV 描述，其列序冻结在
  [contract-design.md §3.14.7](../analysis/contract-design.md)；
- **边界（P1）**：已交付「声明 → 校验 → 绑定」。加载模块并按 stage 调用它的运行时**尚未接线**
  （见 task-9）；探针本身从不注册、也不运行 hook。

## 6. 几何字段分组

几何按内核对象分组，全部位于 owner 段之下：

| 分组 | 段 | 字段（示例） |
|---|---|---|
| task 结构 | `platform.abi.task_struct` | `prio`、`normal_prio`、`pi_lock`、`pi_waiters`、`pi_top_task`、`cred`、`comm`、`tasks`、`seccomp` |
| 内核符号 / 滑移锚点 | `platform.abi.offset`（ABI 级）与 `backend.cve_2026_43499.offset`（route 相关） | `init_task`、`init_cred`、`selinux_enforcing`、`slide_loggers_0_1` |
| 物理映射 | `platform.abi.kernel` | `kernel_phys_load`、`kernel_phys_offset` |
| 凭据模板 | `backend.cve_2026_43499.cred` | `copy_size`、`caps_offset`、`caps_count`、`caps_value` |
| KernelSnitch | `backend.cve_2026_43499.kernel` | `kernelsnitch_collisions`、`mm_struct_sz`、`compact_waiter` |
| route 几何 | `backend.cve_2026_43499.route.<route>` | `select_stack.waiter_shift`、multicast/TCP 调参 |

未使用的 route 专有字段直接省略，不要写 `0` 或占位值。`null` 只出现在半填的模板里，表示
「尚未推导」。完整的 path → 类型清单以 manifest 为准（第 0 节）。

## 7. execution 调优（advisory）

execution 调优由 resolver 从共享片段提供（`execution-tuning.conf` 与
`execution-<route>.conf`）；设备 profile 通过 `include` 引入，只写差异值。字段位于
`backend.cve_2026_43499.execution`（`recommended_cpus`、`heap`、`race`、
`tcp`/`select`、`handoff`）。`execution.selected_cpus` 总是按解析出的 CPU 对重新推导，
设备不会依赖过期值。

## 8. GLKv3 wire

- 文档就是一个 MessagePack 值，根为 **map**；`schema` 必须等于 `3`。**没有 magic、
  没有版本前缀、没有独立头**。
- 键：`schema`、`release`、`backend`、`terminal` 与 `sections`（owner-qualified 段名 →
  「键 → 值」map）。
- 类型：offset/长度用无符号整数；可能为负用有符号整数；bool（显式 `false` ≠ 缺失）；
  token/路径用 UTF-8 字符串；字节块用 bin；另有 array 与 map。
- canonical 编码：最短整数形式、map 键按 UTF-8 字节序、不使用 float；同一逻辑文档必须逐字节一致。
- presence 由键是否出现表达；省略的字段不等于 0。
- 拒绝（fail-closed，在任何阶段之前）：非 map 根、缺 `schema`、`schema != 3`、生产 schema
  下的未知 section/键、类型不符、截断、过深/过大（文档上限 1 MiB）。
- 传输：native 可执行文件的 stdin，4 字节大端长度前缀 + 文档（其后可选一帧运行时密钥）；
  调试时也可用预生成的 `.bin` 文件。运行时密钥绝不进文档。
- extractor 从不产出 wire：它产出 HOCON（`--format conf`，`schema_version = 3`）或旧 JSON 报告。

## 9. 校验与诊断

- `release` 必须与设备 `uname -r` 完全一致（模板永不匹配）；
- schema 必填字段必须在；由 schema 默认物化的值以 `default_used` 报告，让「静默默认」可见；
- 迁移过的旧 step id 会在 stderr 报告；
- 未知键、未知 token、不可用（计划）组合、与 token 不一致的根 route/terminal，以及第 5 节的
  任何插件规则——一律 fail-closed：拒绝文档，而不是部分生效。

## 10. 存储与加载层次

| 层 | 位置 |
|---|---|
| 内置 profile、`index.conf`、模板、片段 | `app/src/main/assets/kernel_profiles/`（只读，随 APK 分发） |
| 导入 / 导出的用户 profile | App 私有 `user_profiles/`（App files 目录下） |
| 高级覆盖 | App 的覆盖存储（逐字段，最后应用） |
| 插件模块 | App 私有 **no-backup** `countermeasures/` 根（模块绝不能进 Android 自动备份） |
| 调试用导出 wire | `./gradlew exportKernelProfiles` → `build/kernel-profiles/*.bin` |

## 11. 命令与对拍测试

```sh
make -C src profile-manifest-v3      # 重新生成 path -> 类型 manifest
make -C src combination-manifest     # 重新生成 token manifest + 解析向量
./gradlew exportKernelProfiles       # 导出 GLKv3 .bin profile
./gradlew :app:testDebugUnitTest :profile-core:test
```

跨语言一致性靠测试、不靠记忆：`ProfileManifestV3AgreementTest`（Kotlin 表 == manifest）、
`BackendMatrixAgreementTest`（`index.conf` 矩阵 == manifest）、
`CombinationTokenAgreementTest` / `CombinationTokenHardcodeTest`（token 来自导出清单、
运行时代码零字面量）、`ProfileLayoutEquivalenceTest` 与 `BuiltinProfilesTest`（每份内置
profile 都能归一并通过校验）、`PluginProbeGoldenTest`（设备探针 golden）、
`LegacyProfileConverterTest`（唯一迁移点）。

## 12. 旧 JSON 导入

旧的 `offsets.json` 报告仍可导入。转换**只在 App 侧**发生（`LegacyProfileConverter`，
唯一迁移点），产出当前的 `schema_version = 3` HOCON；native 可执行文件没有 JSON 或旧格式
解码器。同一个转换器把**更早一代**的 HOCON profile（已废弃的版本号，或缺该键）归一为 `3`
并记诊断；其它版本值一律拒绝，错误信息带实际值。

## 13. 修改配置的检查清单

1. 先决定 owner 段：新字段放在**消费它的组件**所属段。
2. 在 native 侧声明（`FieldSpec`）并重新生成 manifest；**不要手改 manifest**。
3. 若字段决定行为，优先扩展 token 目录，而不是加 CLI 开关或第二个选择键。
4. Kotlin 侧通过生成的 manifest 更新，不要写字面量。
5. 把字段补进对应模板/profile；片段保持不带 `schema_version`。
6. 可选字段必须在 schema 里给出默认值；否则必须 required，缺失即 fail-closed。
7. 跑门禁：`make -C src native-host-tests`、NDK 构建（零告警）、
   `make -C src lint-tidy`、Gradle 测试；改动触及攻击路径时另跑真机门禁。
8. 结构变化时，同批更新唯一权威图
   [full-process-uml.md](../development/full-process-uml.md)。
