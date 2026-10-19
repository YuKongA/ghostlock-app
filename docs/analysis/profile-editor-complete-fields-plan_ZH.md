[English](profile-editor-complete-fields-plan.md) | [简体中文](profile-editor-complete-fields-plan_ZH.md)

# Profile 编辑器完整字段与 HOCON 完整性计划（2026-09-30）

## 状态与基线

- 分支 `very-not-stable-dev`；基线 commit `deff0b1`（Merge branch
  'ancillary-architecture'）。
- 触发：MTK moto edge 40（`6.6.127-android15-8-gb947b5758b2a-ab15580855-4k`）
  适配中，`kernel_phys_load` 在 UI 找不到，extractor conf 无法被 Kotlin 读取。
- 基线行为：
  1. 「参数覆盖」页（`ProfileOverrideScreen`）只渲染 `ExecutionEditor`，字段来自
     `ProfileConfig.GeneralPaths`（`GhostlockModels.kt:95`），共 9 个
     `execution.*` 项加当前 route 调参。`kernel_phys_load`、`task_struct.*`、
     `cred.*`、`offset.*` 都不出现。
  2. 「高级参数覆盖」页（`AdvancedOverrideScreen`）用 `buildTree(full)` 只遍历
     已解析 profile 中存在的键；字符串值被 `value is Number` / `value == null`
     跳过，无法编辑 profile 未携带的键。
  3. extractor `render_conf` 按无值即省略输出，`kernel_phys_load` 为十六进制
     无引号（`0x{phys:X}`）。
  4. HOCON（Typesafe Config）把 `0x...` 当 unquoted string；Kotlin `getLongAt`
     （`ValueModel.kt:56`）只取 `Number`，extractor 的 `kernel_phys_load` 因此被
     静默丢弃（实测 `0x40080000` 得到 `String`，`1074266112` 得到 `Integer`）。
  5. 内置 profile 使用 `include` 且不携带全部字段。

## 目标与约束

1. UI 编辑器完整加载当前 route 可编辑的每一项；profile 未携带的项以 `null`
   占位并显示可填。
2. 所有内置或生成的 HOCON 包含每一项（公共几何、当前 route、fallback）。
   能自动推定的自动推定，否则写 `null`。
3. extractor 只输出十进制数字，不产生 `0x`、字符串或科学计数，并写入每一项。
4. 写入与导出完整保留每一项（含 `null`）。

非目标：

- 不改攻击路径（waiter/race/payload/route/exec 流程）、不改 `src/core/` 的 native
  解析逻辑。
- 不新增 route 或组件，不改 GLK1 wire 与 HOCON 传输格式版本（仍为 v2）。
- 不动 `kernelsnitch/`、`LegacyProfileConverter.kt` 的 v1 转换、规范中的明确保留
  清单。
- 不改变现有内置 profile 的正确数值，只补齐字段与表示。

## 改动（已实施）

| 文件 | 改动 |
|---|---|
| `tools/extract_rs/src/report.rs` | `kernel_phys_load` 改十进制；`conf_offsets`/task/cred/route geometry/snitch 缺失项写 `null`；route geometry 对当前 route 全字段占位 |
| `tools/extract_rs/src/main.rs` | conf 组装传入完整字段集合（含 null 占位） |
| `app/src/main/assets/kernel_profiles/*.conf` | 逐份补齐公共与 route 字段，未知写 `null` |
| `app/src/main/kotlin/.../data/AndroidProfileConfigController.kt` | `buildTree`/`generalFields`/`routeTuningPaths` 按当前 route、fallback、公共字段的全集渲染，缺失补 `null`；`buildTree` 跳过 `execution` |
| `app/src/main/kotlin/.../domain/model/GhostlockModels.kt` | `GeneralPaths` 扩展为 execution 全字段 |
| `profile-core/.../data/HoconSupport.kt` | 核对 `render` 对 `null` 的输出，补测试 |
| `docs/kernel_profiles/PROFILE_SCHEMA*.md` | 回写字段完整性规则 |

数据流从「每层可能静默丢失字段」变为「presence 显式、字段完整」：

```
extractor conf --(十进制 + null 占位)--> HOCON(含每一项) --(Number/null 保留)--> resolved map
   --(权威全集 + null 占位)--> UI 完整列出 --(可编辑/可写回)--> 导出/运行
```

不变量：

- 攻击关键路径与 native 解析不变。
- v2 的「键是否出现」语义不变；`null` 与「提供了 0」的区分由 D3 定义。

## 决策记录

- D1 完整加载含公共项：UI 编辑器列出当前 route 几何、fallback、公共几何
  （`task_struct`/`cred`/`offset`/`kernel`/`kernelsnitch`），缺失显示 `null`。
- D2 execution 放一般编辑页：`execution.*` 在 `ProfileOverrideScreen` 完整加载，
  不进高级树，不内联进设备 profile HOCON。
- D3=A：`null` 表示键不出现在 GLK1 binary（native 读作未提供）。HOCON/UI 保留
  `null` 仅作占位，不改变 wire 的 presence 语义，也不引入 presence 位图。
- D4：内置 profile 逐份手工补 `null`（基线 `deff0b1` 下共 54 份设备 profile）。
- D5 可执行 branch：extractor 只输出被选中的可执行 route branch；有 fallback 时
  连同 fallback branch 的 geometry 一并输出，`fallback.to` 写实际 route。

GLK1 v2 是 Kotlin 与 native 之间的传输格式，也是内置预编译 profile 的载体
（`exportKernelProfiles` 产出 `.bin`，native `--load-prebuilt-profile`）。它不是
用户编辑的保存格式（保存为 HOCON/preferences），也不是执行格式。因此 D3=A
不改 GLK1 字节格式，只改 HOCON/UI/extractor 三层。

## 5.15 multicast→select fallback（deferred）

状态：按用户决定推迟，本批不触碰 `route_policy.hpp`，extractor 也不输出 fallback
branch。

需求：5.15 设备同时具备 mcast 几何与可推导的 pselect 时，主用 multicast，失败
回退 select。

- 基线：`run_route_policy` 的 fallback 分支受 `Policy::allows_fallback` 约束
  （`route_policy.hpp:237`）。`SelectPolicy` 与 `MulticastPolicy` 继承默认的
  `false`，只有 `TcpPolicy` 为 `true`（`route_policy.hpp:93`）。extractor 把
  fallback 硬编码为 `to="none"`。
- 计划改动：`MulticastPolicy::allows_fallback = true`；extractor 在有 pselect 时
  输出 `fallback.to="select_stack"` 加 `fallback.route.select_stack.waiter_shift`。
- 阻塞项：fallback 只切换 `run()`，但 backend 的 W2/W3 能力仍按主 policy 编译
  （`multicast=true`、`w2_fast_repair=true`，hook 定义在 `MulticastPolicy`）。
  回退到 select 后这些 capability 与 hook 是否成立，有两种处理：
  - (a) 仅允许 multicast 在特定 clean 失败点回退，且回退后不进入 multicast 专属
    W2 快修；
  - (b) 为 fallback 增加独立 capability 判定，禁止跨 primitive 复用 W2/W3。
  须补充控制流图与不变量后再实现。
- 验证门槛（触及攻击路径）：`cmp_disasm` 8 函数、冷机真机门禁、归档。
- 当前仍待确认：是否先只做 extractor/UI/HOCON 三项（不触 native），把 native
  fallback 作为独立批次。

## 已澄清的非阻塞项

`route.tcp_zerocopy.compact_waiter` 经 `ProfileResolver.nativeValue` 的 branchField
映射落到 native `misc.compact_waiter`（`binary.cpp:113` 的
`OPT("compact_waiter", misc.compact_waiter)`），与 6.1 内置 profile 一致，无冲突。
route HOCON 字段全集为 `tcp_zerocopy.{compact_waiter}`、
`select_stack.{waiter_shift}`、
`multicast_waiter.{waiter_off,buffer_size,task_offset,lock_offset,compact_waiter}`。

## 兼容性与回滚

旧内置 profile 仍可解析（新增字段均可选或为 null）。各文件独立，可按文件 revert；
extractor 输出格式变更只影响新生成的 conf。

## 验证证据

- [x] `cargo test --release --manifest-path tools/extract_rs/Cargo.toml`：全过，
  新增 conf 十进制与字段完整含 null 用例。
- [x] `./gradlew :app:testDebugUnitTest`、`:profile-core:test`：全过。
- [x] `./gradlew exportKernelProfiles`：导出的 `.bin` 含 `kernel_phys_load` 等字段。
- [x] 内置 profile 补全：顶层 `kernel_phys_load`、`offset.empty_zero_page`、
  `cred.usage_offset`，共享 `credential-6x.conf`/`kernelsnitch-6x.conf` 补全。

## 剩余工作

- [ ] native route-aware fallback（deferred，见阻塞项）。
- [ ] 真机门禁：加载新 profile，确认 route 命中与写验证通过。改动未触 native
  攻击路径，但字段完整性需真机确认一次。
