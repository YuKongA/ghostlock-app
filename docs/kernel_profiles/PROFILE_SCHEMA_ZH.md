# Kernel Profile 结构文档（配置系统）

> English: [PROFILE_SCHEMA.md](PROFILE_SCHEMA.md)

本文档描述 `app/src/main/assets/kernel_profiles/` 下内核配置（profile）的完整结构、字段语义、路由机制与校验规则，以及配置在 Kotlin / native 之间的流转方式。

> 适配新内核的操作步骤见 [README_ZH.md](README_ZH.md)；`execution` 调优建议见 [defaults_ZH.md](defaults_ZH.md)。

> **三个"版本"是彼此独立的东西**（不要当成同一条版本序列）：
> - **Profile schema 版本**：HOCON 配置代际，由每份 profile 里的 `schema_version` 字段表达（见第 2 节）。
> - **Binary wire 版本**：magic `0x0D000721` 之后的 GLK1 传输版本。当前 writer 写 wire version `3`
>   （组件 id + middleware 节 + options 节）；wire version `2` 文档仍可解码，但 v2 writer 已移除。
> - **v1 JSON 导入**：remote/main 时代的 `offsets.json`，由 `src/core/legacy/`
>   （`convert_legacy_offsets`）与 Kotlin `LegacyProfileConverter` 一次性转换。

## 0. 文件格式（HOCON）

内置配置、`index.conf`、共享配置与导入的偏移文件都按 **HOCON** 解析：

- JSON 是 HOCON 的子集，历史文件无需转换；
- 支持 `#` / `//` 注释、尾逗号、`${var}` 变量替换（可选替换写 `${?var}`）；
- 支持 `include "file.conf"`（相对同目录、可嵌套、防循环）：读取 assets 时由 `AssetConfigLoader` 展开；
  **导入**时优先用一并选中的文件展开，其次查包内共享文件，缺失会报错并要求重新选择；
- 解析发生在 Kotlin 侧（`HoconSupport`），随后以类型化二进制结构体传给 native（见第 9 节）；
- 应用内部存储与导出均为 HOCON（`ghostlock-extract --format conf` 输出 flatten 自包含
  profile，走常规导入路径；`--format json` 与旧 `offsets.json` 仍属 legacy 路径）。
- 提取器输出是**候选来源**：镜像实际获得多少字段就写出多少，未获得的字段省略，不会借用
  相邻内核族的猜测值。因此候选 profile 可能不完整；导入成功后由 App 的字段校验填充
  `invalidPaths` 并在执行前拦截，生成成功不等于设备受支持。

## 1. 数据流总览

```
assets/kernel_profiles/<release>.conf     内置配置（HOCON）
assets/kernel_profiles/execution-tuning.conf   execution 通用调优（所有内核）
assets/kernel_profiles/execution-<route>.conf   各路由的 execution 调优 preset（由 resolver 加载）
assets/kernel_profiles/credential-6x.conf      6.x 凭据模板共享值
assets/kernel_profiles/kernelsnitch-6x.conf    6.x KernelSnitch 共享值
assets/kernel_profiles/<major.minor>-template.conf  参考模板（登记 index，调试页可手动加载；不参与设备匹配与自动回退）
filesDir/offsets.conf                     解析/导入的偏移（imported，HOCON）
内部覆盖（advanced override，sparse）    高级参数覆盖页写入
        │
        ▼  ProfileConfigController.resolve（Kotlin）
   resolved profile（单对象，全字段已合并）
        │
        ├──▶ 运行：类型化二进制（direct 与 Shizuku 都把字节写入 native 的 stdin）
        │      native 只做解析与几何使用，不再做参数校验
        ├──▶ 快照：filesDir/<release>.conf（HOCON，导出配置的源）
        └──▶ UI：参数覆盖页 / 高级参数覆盖树
```

合并优先级（低 → 高）：`execution-tuning`（+ 按路由的 `execution-<route>.conf`）→ `内置/profile + imported offsets` → `高级覆盖`；最后强制写入 `execution.selected_cpus`（来自手动选择或 imported/覆盖中的显式值）。

## 2. 顶层结构

```hocon
# GhostLock kernel profile (HOCON; JSON stays valid)
release = "6.6.77-android15-8-gca30f3b4bef6-abogki440974771-4k"
schema_version = 1
kernel_major = 6
route {
  select_stack { waiter_shift = -2 }
}
fallback { to = "none" }
kernelsnitch { collisions = 4, mm_struct_sz = 4096 }
task_struct { prio = 132, cred = 2080, pi_lock = 2316 }
cred { copy_size = 136, caps_offset = 48, caps_count = 5 }
offset { init_task = 34464384, init_cred = 34538824 }
# execution 调优由 resolver 从 preset 文件提供；只写差异
```

| 字段 | 类型 | 说明 |
|---|---|---|
| `release` | string | 必须与设备 `uname -r` 完全一致（区分大小写/后缀）；模板文件不参与匹配 |
| `schema_version` | int | 固定为 `1` |
| `kernel_major` | int | `5` 或 `6`；用于地址解析与合法性检查，不再决定路由 |
| `route` | object | 显式路由父项，只含一个分支，见第 3 节 |
| `fallback` | object | 回退声明（`to` + 可选 `route` 分支），见第 3 节 |
| 几何字段 | object/int | 按命名空间分组：`task_struct` / `cred` / `offset` / `kernelsnitch`；未采用的路由专属字段**直接省略**，不要写 `0` 或占位值；`null` 只出现在模板与编辑中间态 |
| `execution` | object | 调优参数（advisory）；通用项来自 `execution-tuning.conf`，路由项来自 `execution-<route>.conf`；两者由 resolver 作为 preset 加载，设备 profile 不 include。只写差异 |

## 3. 路由（route）机制

路由不再由内核版本或字段存在性推断，而是由配置显式声明。`route` 是父项，**只允许一个分支**：

```hocon
route { tcp_zerocopy { compact_waiter = 1 } }
route { select_stack { waiter_shift = -2 } }
route { multicast_waiter { waiter_off = 96, buffer_size = 264 } }
```

- 旧配置缺少 `route` 时保留兼容推断：`kernel_major==5 且 mcast.waiter_off>0` → `multicast_waiter`；否则 `compact_waiter!=0` → `tcp_zerocopy`；否则 `select_stack`（导入旧 `offsets.json` 时自动归一为新结构）。
- 每个路由只要求自己的分支：未采用的路由分支完全不写；分支之间不得并存（一个配置一种 path）。
- `fallback` 父项声明回退：`"to"` 取 `"none"` 或路由名；声明目标时同时给出 `"route"` 分支，
  存放回退执行所需的字段：
  ```hocon
  fallback {
    to = "select_stack"
    route { select_stack { waiter_shift = 1 } }
  }
  ```
  当前实现支持 `tcp_zerocopy` 失败后回退 `select_stack`；写 `"to": "none"` 即关闭。
- native 侧 `RouteKind` 枚举与 Kotlin 侧取值一一对应（`profile.h` / `ProfileConfig.Routes`）。

### 3.1 remote/main 时代 offsets 的兼容转换

上游（`remote/main` 顶端提交 `bddfea46`）时期只有 6.x 内核、没有 Shizuku 路径、没有 5.x 几何；其 `offsets.json` 条目是提取器报告格式（`symbols`/`struct_fields` + 4 个顶层标量 + 元数据）。加载时会由 `LegacyProfileConverter` 自动归一（幂等）：

| 上游内容 | 转换结果 |
|---|---|
| `symbols` 对象（`off_*` 键） | `offset.*` 命名空间 |
| `struct_fields` 对象（`task_*` 键） | `task_struct.*` 命名空间；`rt_mutex_waiter`/`cred_uid`/`seccomp_*` 等 BTE 专用键保留原组，不参与新校验与比较 |
| 顶层 `pselect_waiter_shift` | `route.select_stack.waiter_shift`；tcp 配置则保留为 `fallback.route.select_stack.waiter_shift` |
| 顶层 `compact_waiter` / `mm_struct_sz` | `route.tcp_zerocopy.compact_waiter` / `kernelsnitch.mm_struct_sz` |
| `kimage_text_base` / `btf_size` / `kallsyms` | 丢弃 |
| 无 `route` 字段 | 按 6.x 几何推断：`compact_waiter` → tcp，否则 select |
| 无 cred 模板 | 由转换器写入内置 6.x 常量（`credential-6x.conf` / `kernelsnitch-6x.conf`）；5.x 凭据字段仍由作者提供 |

上游文档不可能选中 5.x 分支（`multicast_waiter` 仅作为本地格式的受保护推断保留）。

### 必填矩阵

| 字段组 | 公共（所有路由） | tcp_zerocopy | select_stack | multicast_waiter |
|---|:---:|:---:|:---:|:---:|
| `offset.init_task` / `offset.init_cred` / `offset.root_task_group` / `offset.selinux_enforcing` | 必填 | | | |
| `task_struct.prio` / `task_struct.pi_lock` / `task_struct.pi_waiters` / `task_struct.pi_blocked_on` / `task_struct.cred` / `task_struct.seccomp` | 必填 | | | |
| `kernel_major` ∈ {5,6}、`cred.copy_size`、`cred.caps_count` 及凭据模板边界 | 必填 | | | |
| `route.tcp_zerocopy.compact_waiter` / `route.multicast_waiter.compact_waiter` | | 必填 | | 必填 |
| `route.select_stack.waiter_shift` | | | 必填（0 合法） | |
| `route.multicast_waiter.waiter_off`（>0）、`route.multicast_waiter.buffer_size`、`route.multicast_waiter.task_offset`、`route.multicast_waiter.lock_offset`、`route.multicast_waiter.fake_lock_offset`、`route.multicast_waiter.fake_task_offset`、`route.multicast_waiter.lock_slots_offset`、`route.multicast_waiter.lock_slot_count`、`route.multicast_waiter.lock_slot_stride`、`offset.mcast_fake_bss`、`offset.empty_zero_page`、`kernelsnitch.mm_struct_sz`、`cred.ref_count`（>0） | | | | 必填 |

凭据模板边界（通用）：`cred.usage_offset + 4 ≤ cred.copy_size`；`cred.caps_offset + cred.caps_count × 8 ≤ cred.copy_size`；`cred.ref_count ≤ 4`；每个 `cred.refN_image` 非零、`cred.refN_offset + 8 ≤ cred.copy_size`。`multicast_waiter` 还要求 `cred_copy_size ≥ 0xa0` 且 `route.multicast_waiter.waiter_off + route.multicast_waiter.lock_offset + 8 ≤ route.multicast_waiter.buffer_size`。

## 4. 几何字段分组

### 4.0 内核对象与字段映射

配置字段名即内核结构成员名（`struct` 内偏移）：

```
task_struct
├── prio             → task_prio
├── normal_prio      → task_normal_prio
├── sched_task_group → task_sched_task_group
├── pi_lock          → task_pi_lock
├── pi_waiters       → task_pi_waiters
├── pi_top_task      → task_pi_top_task
├── pi_blocked_on    → task_pi_blocked_on
├── pid / tgid       → task_pid / task_tgid
├── atomic_flags     → task_atomic_flags
├── real_cred        → task_real_cred
├── cred             → task_cred
├── comm / tasks     → task_comm / task_tasks
└── seccomp          → task_seccomp
```

```
cred
├── usage            → cred_usage_offset / cred_usage_value
├── cap_*            → cred_caps_offset / cred_caps_count / cred_caps_value
└── 引用修复点        → cred_ref_count + cred_refN_offset / cred_refN_image
```

```
伪造多播对象（multicast_waiter 路由）
├── waiter 起点      → route.multicast_waiter.waiter_off
├── task / lock      → route.multicast_waiter.task_offset / lock_offset
├── fake_lock / task → route.multicast_waiter.fake_lock_offset / fake_task_offset
└── lock slots       → route.multicast_waiter.lock_slots_offset / count / stride
```

`offset` 命名空间是内核映像符号偏移（`init_task`、`init_cred`、`empty_zero_page` 等），其中 `offset.slide_*` 是 KASLR 滑移探测锚点；它们与 `kernel_phys_load` 一起用于把符号地址换算为运行地址。

### 4.1 任务结构偏移（`task_struct`）

| 字段 | 含义 |
|---|---|
| `task_struct.prio` / `task_struct.normal_prio` | 任务优先级 / 常规优先级（PI 提升判定） |
| `task_struct.sched_task_group` | `sched_task_group` 偏移 |
| `task_struct.pi_lock` / `task_struct.pi_waiters` / `task_struct.pi_top_task` / `task_struct.pi_blocked_on` | `pi_lock`、`pi_waiters`、`pi_top_task`、`pi_blocked_on` |
| `task_struct.pid` / `task_struct.tgid` | PID / TGID |
| `task_struct.atomic_flags` | `atomic_flags`（用于现场清理判定） |
| `task_struct.real_cred` / `task_struct.cred` | real cred / cred 指针 |
| `task_struct.comm` / `task_struct.tasks` / `task_struct.seccomp` | `comm`、任务链表、`seccomp` |

### 4.2 凭据模板（`cred`）

| 字段 | 含义 |
|---|---|
| `cred.copy_size` | 凭据结构整体拷贝大小 |
| `cred.usage_offset` / `cred.usage_value` | 引用计数字段偏移 / 目标值 |
| `cred.caps_offset` / `cred.caps_count` / `cred.caps_value` | capability 集合偏移 / 数量 / 填充值 |
| `cred.ref_count` | 需要修复的引用字段数量（≤4） |
| `cred.refN_offset` / `cred.refN_image`（N=0..3） | 各引用字段偏移 / 应恢复的镜像值 |

### 4.3 内核符号与滑移（`offset`）

| 字段 | 含义 |
|---|---|
| `offset.init_task` / `offset.init_cred` | `init_task` / `init_cred` 相对内核镜像基址的偏移 |
| `offset.root_task_group` | `root_task_group` 偏移 |
| `offset.selinux_enforcing` | `selinux_state.enforcing` 偏移（W1 写 0） |
| `offset.selinux_blob_sizes` / `offset.security_hook_heads` | SELinux/安全钩子相关偏移 |
| `offset.slide_nfulnl_logger` / `offset.slide_boot_id` / `offset.slide_loggers_0_1` | 地址滑移（KASLR）定位点 |
| `kernel_phys_load` | 内核物理加载地址（0 时按 SoC 公式回退） |
| `recommend_shizuku` | 该内核是否建议走 Shizuku（0/1，所有 profile 必填，默认 0；仅提示不强制）。不在任何编辑器中显示：推荐内核在**每次启动时自动打开**主页“Run via Shizuku”开关，会话内可手动关闭，关闭后本次会话不再要求 Shizuku |

### 4.4 select_stack / tcp 路由字段

| 字段 | 含义 |
|---|---|
| `route.select_stack.waiter_shift` | select 路由 waiter 在栈上的相对位移（0 合法）；回退声明下对应 `fallback.route.select_stack.waiter_shift` |
| `route.tcp_zerocopy.compact_waiter` | tcp 路由的紧凑 waiter 布局标记；multicast 分支下同样需要（`route.multicast_waiter.compact_waiter`） |

### 4.5 multicast_waiter 路由字段（`route.multicast_waiter`）

| 字段 | 含义 |
|---|---|
| `route.multicast_waiter.waiter_off` | 多播缓冲区中 waiter 的偏移（必须 > 0） |
| `route.multicast_waiter.buffer_size` | 伪造缓冲区大小 |
| `route.multicast_waiter.task_offset` / `route.multicast_waiter.lock_offset` | 缓冲区中任务 / 锁字段偏移 |
| `route.multicast_waiter.fake_lock_offset` / `route.multicast_waiter.fake_task_offset` | 伪造锁 / 任务对象偏移 |
| `route.multicast_waiter.lock_slots_offset` / `route.multicast_waiter.lock_slot_count` / `route.multicast_waiter.lock_slot_stride` | 锁槽数组偏移 / 数量 / 步长 |
| `offset.mcast_fake_bss` | 伪造 BSS 页内核映像偏移 |
| `offset.empty_zero_page` | `empty_zero_page` 偏移 |

### 4.6 KernelSnitch 参数（`kernelsnitch`）

所有路由通用（KernelSnitch 用于 `mm_struct` 泄漏搜索），不随路由取舍：

| 字段 | 含义 |
|---|---|
| `kernelsnitch.collisions` | 需要的 futex 碰撞数量 |
| `kernelsnitch.mm_struct_sz` | `mm_struct` 的 SLUB 大小（未填写时使用内置默认） |

> 旧版扁平键（`kernelsnitch_collisions` / `mm_struct_sz` / `task_*` / `cred_*` / `off_*` / `mcast_*`）在导入旧 `offsets.json`、解析 extractor 输出或读取高级覆盖时自动归入对应命名空间。
> 应用内部存储（`offsets.conf`、高级覆盖、快照）与导出均为 HOCON；旧版 JSON 缓存**不迁移、启动即丢弃**。

## 5. execution 调优参数（advisory）

`execution` 全部为建议值，随 profile 合并后传入 native；数值语义与默认值见 [defaults_ZH.md](defaults_ZH.md)。通用分组来自 `execution-tuning.conf`，路由分组由 resolver 从 `execution-<route>.conf` 加载（主路由 + 回退路由），设备 profile 不再 include 它们：

- `recommended_cpus` / `selected_cpus`：推荐与本地选定核心（`selected_cpus` 由参数覆盖页“一般参数覆盖”或主页 CPU 选择维护）
- `heap`：KernelSnitch 搜索的尝试次数与超时
- `race`：路由竞态等待/稳定/轮询间隔
- `stages`：W1/W2/W3 尝试次数与稳定时间
- `routes.tcp_zerocopy` / `routes.select_stack` / `routes.multicast_waiter`：各路由的重试与等待参数，分别位于 `execution-tcp-zerocopy.conf` / `execution-select-stack.conf` / `execution-multicast-waiter.conf`；Kotlin 在合成 native 文档时会为缺失的路由组补默认值，因此 native 始终收到完整 `routes`
- `handoff`：root 交接与 KernelSU 加载轮询

## 6. 校验与反馈

校验在 Kotlin（`AndroidProfileConfigController.validateProfileFields`）完成：

1. 按第 3 节矩阵检查公共与所选路由的字段：缺失（`null`）或为 `0` 的必填项、越界组合（凭据/多播边界）都会记录到 `ProfileConfig.invalidPaths`。
2. 参数覆盖页与高级参数覆盖中，非法项以红色 label 显示（未填写同样标红）；已覆盖且合法项为黄色。
3. `fallback.to` 必须是 `"none"` 或合法路由名；声明回退时，目标分支的必填字段同样会被校验（如回退 `select_stack` 需要 `fallback.route.select_stack.waiter_shift` 存在，0 合法）。
4. 主页“执行”按钮在 `invalidPaths` 非空时置灰，点击提示修正红色项；即使绕过，`runExploit` 也会在启动 native 前拦截并写入日志。
5. native 不再做几何校验，只解析 v3 二进制（v2 仍可解码）并按组件选择与字段执行。

## 7. 加载层次与存储位置

| 层 | 来源 | 位置 | 写入者 |
|---|---|---|---|
| shared | 共享值；tuning preset 由 resolver/exporter 加载，core 共享值由设备 profile `include` | `execution-tuning.conf` / `execution-<route>.conf` / `credential-6x.conf` / `kernelsnitch-6x.conf` | 随包发布 |
| builtin | 精确 `uname -r` 命中；未命中即视为不支持 | `assets/kernel_profiles/*.conf` | 随包发布 |
| imported | 解析/导入的偏移（同一 release entry） | `filesDir/offsets.conf` | 解析 OTA / 导入配置文件 |
| general override | `execution.*` | `filesDir/offsets.conf` 的 release entry | 参数覆盖页“一般参数覆盖 / 重设参数” |
| route override | `route` / `fallback.to` | `filesDir/offsets.conf` 的 release entry | 高级配置覆盖页“路由 / 回退” |
| advanced override | 任意数值路径（sparse，HOCON 文本） | 内部 `debug_profile_overrides` | 高级配置覆盖（自动保存） |
| 快照 | 合并后的完整 HOCON | `filesDir/<release>.conf` | 任何覆盖保存后、导出前 |
| 手动内置来源 | 指定其他真实 release（危险） | 内部 `debug_builtin_release` | 参数页“加载其他内置配置（危险）”；覆盖仍绑定本机 release |

导出配置：把该 release 的合并快照（HOCON 单文件，include 已合并、已按路由精简调优项）写入用户选择的文件夹（SAF）。

## 8. 模板配置（*-template）

- 每个大版本一个：`5.15-template` / `6.1-template` / `6.6-template` / `6.12-template`，登记在 `index.conf`，仅作**开发与调试参考**。
- 所有几何/偏移字段为 `null`（未填写），`route` / `fallback` 给出完整分支结构；每个字段上方都有中文说明注释，可直接复制填写。
- 在内置选择页的“模板（参考，未填写）”分区可手动加载，用于查看字段结构；模板**不参与设备匹配，也不做自动回退**——设备无精确命中且无 imported 时视为不支持。
- 模板只含 core 字段（以及 core 的 include）；execution 调优由 resolver 从 `execution-*.conf` 提供，复制模板无需补 tuning include。仓库内的同步副本见 `docs/kernel_profiles/templates/`，assets 中的原文件可用 adb 查看。

## 9. native 传输与解析

运行时的配置传输是**类型化二进制结构体**（v3），不再是 JSON 文本；v2 文档仍可解码以兼容。

- Kotlin 侧由 `NativeProfileDocument`（data class，与 native `struct kernel_offsets` 一一对应）经 `toBinaryV3()` 序列化：
  `u32 magic(0x0D000721) + u16 version(3) + u16 frontend_id + u16 backend_id + u16 middleware_id + u8 kernel_major + u8 fallback_route + u16 release_length + release + 68×u64 core 槽 + u16 middleware 条目数 + N×(u8 key_length + key + i64) + u16 option 条目数 + M×(u8 key_length + key + i64) 小端`；`middleware_id` 承载 route，`recommend_shizuku` 仅属 App、不再上 wire。字段顺序见 `NativeProfile.kt`（`flattenCommon` / per-route `RouteConfig`）与 `profile/binary.cpp`（`kCommonFields` / `kTcp/kSelect/kMulticastFields`），两侧必须同步修改。
- 传输路径：direct 与 Shizuku 都把 profile 以 **stdin** 交给 native（`--ghostlock-app-call`），不再落盘 `active-profile.bin`、也不再使用 `--profile`。
- native 只有一条解码路径：`profile/entry.cpp` 把 stdin（或文件）字节交给 `profile/binary.cpp::parse`。它不检测 magic 之外的格式，也没有 JSON 回退——v1 JSON 只在导入转换（`legacy/offsets_json.cpp`）里解析。
- 内部存储与“导出配置”均为 HOCON（人类可读）；JSON 只出现在 v1 转换（旧 `offsets.json`、`ghostlock-extract --format json`）。
- 运行时路由与能力判断（`TargetProfile::route()`、`TargetProfile::supports()`、`route_capability`）全部基于解析后的 route。

## 10. 修改配置的检查清单

1. 只改需要改的字段；未采用的路由字段直接省略（不要补 `0` 占位）。
2. `route` 只能有一个分支，且分支内必须给出该路由的必填字段；`fallback.to` 声明了回退目标时，`fallback.route` 分支内同样要补齐。
   共享 core 值通过 `include` 引入，不要复制：`credential-6x.conf`（6.x 凭据模板）、`kernelsnitch-6x.conf`（6.x collisions）。execution 调优（`execution-tuning.conf` / `execution-<route>.conf`）由 resolver 作为 preset 加载，设备 profile 不要 include。
3. 修改 `execution` 需要设备实测依据；否则保持 defaults。
4. 本地验证：`make native-host-tests`（profiles 解码/校验向量）与 `./gradlew :app:assembleDebug`。
5. 修改字段命名/分组时同步更新：`FieldLabels.kt` + `values*/strings.xml`、可能的 `docs/kernel_profiles/defaults*.md`。
