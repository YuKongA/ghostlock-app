# Profile 编辑器完整字段与 HOCON 完整性计划（2026-09-30）

## 现状与基线

- 分支：`very-not-stable-dev`；基线 commit `deff0b1`（Merge branch 'ancillary-architecture'）。
- 触发：MTK moto edge 40（`6.6.127-android15-8-gb947b5758b2a-ab15580855-4k`）适配中，
  `kernel_phys_load` 在 UI 找不到、extractor conf 无法被 Kotlin 读取。
- 当前行为（已核实）：
  1. UI「参数覆盖」页（`ProfileOverrideScreen`）只渲染 `ExecutionEditor`，字段来自
     `ProfileConfig.GeneralPaths`（`GhostlockModels.kt:95`），共 9 个 `execution.*` 项 +
     当前 route 调参；`kernel_phys_load`、`task_struct.*`、`cred.*`、`offset.*` 均不出现。
  2. UI「高级参数覆盖」页（`AdvancedOverrideScreen`）用 `buildTree(full)`（`AndroidProfileConfigController.kt:695`）
     只遍历**已解析 profile 里存在的键**；字符串值被 `value is Number` / `value == null`
     双重跳过；无法编辑 profile 未携带的键。
  3. extractor `render_conf`（`tools/extract_rs/src/report.rs:243`）按「无值即省略」输出，
     且 `kernel_phys_load = 0x{phys:X}`（`:257`）为**十六进制无引号**。
  4. HOCON（Typesafe Config）把 `0x...` 当 unquoted string；Kotlin `getLongAt`
     （`ValueModel.kt:56`）只取 `Number`，故 extractor 的 `kernel_phys_load` 被静默丢弃
     （已实测：`0x40080000` -> `String`，`1074266112` -> `Integer`）。
  5. 内置 profile 使用 `include` 且不携带全部字段（如 `kernel_phys_load`、其它 route 分支、
     部分 cred 键）。

## 目标与约束

目标：
1. UI 编辑器完整加载**当前 route 可编辑的每一项**；profile 未携带的项以 `null` 占位并显示可填。
2. 所有内置/生成的 HOCON 必须**包含每一项**（公共几何 + 当前 route + fallback）；能自动推定的
   自动推定，否则写 `null`。
3. 检查并修正 extractor 输出：不得产生十进制的数字以外的表示（含 `0x`、字符串、科学计数）；
   并完整写入每一项（含 `null`）。
4. 写入/导出完整保留每一项（含 `null`）。

约束（非目标）：
- 不改攻击路径（waiter/race/payload/route/exec 流程）、不改 `src/core/` 任何 native 解析逻辑。
- 不新增 route / 组件；不改 GLK1 wire 与 HOCON 传输格式版本（仍为 v2）。
- 不动 `kernelsnitch/`、`LegacyProfileConverter.kt` 的 v1 转换、规范中的「明确保留」清单。
- 不改变现有内置 profile 的**正确数值**，只补齐字段与表示。

## 改动清单（逐文件）

| 文件 | 改动 | 理由 |
| --- | --- | --- |
| `tools/extract_rs/src/report.rs` | `render_conf`：`kernel_phys_load` 改十进制；`conf_offsets`/task/cred/route geometry/snitch 缺失项写 `null` 而非省略；route geometry 对当前 route 全字段占位 | 保证可被 Kotlin 读取 + 字段完整 |
| `tools/extract_rs/src/main.rs` | conf 组装处传入完整字段集合（含 null 占位） | 配合上一条 |
| `app/src/main/assets/kernel_profiles/*.conf` | 逐份补齐公共/route 字段；未知写 `null` | 满足「所有配置包含每一项」 |
| `app/src/main/kotlin/.../data/AndroidProfileConfigController.kt` | `buildTree`/`generalFields`/`routeTuningPaths`：以「当前 route + fallback + 公共字段」全集的**权威列表**渲染，缺失补 `null` 行 | UI 完整加载 |
| `app/src/main/kotlin/.../domain/model/GhostlockModels.kt` | 定义「route 可编辑字段全集」常量（route 几何 + 公共几何） | 单一权威 |
| `profile-core/.../data/NativeProfile.kt` | 明确 `null` 的写入语义（见决策点 D3） | 跨层契约 |
| `profile-core/.../data/HoconSupport.kt` | 核对 `render` 对 `null` 的输出（已支持，补测试） | 导出保留 null |
| `src/core/route/route_policy.hpp` | `MulticastPolicy::allows_fallback = true`（5.x mcast→select 回退） | 放开 5.15 fallback（攻击路径，见专节） |
| `docs/kernel_profiles/PROFILE_SCHEMA*.md` | 回写字段完整性规则 | 文档同步 |
| `README*.md`（如需） | 说明生成的 conf 为完整字段 | 双语同步 |

## 数据流/控制流差异

现状（字段在每层可能被静默丢失）：

```
extractor conf --(省略/0x hex)--> Kotlin HOCON --(非 Number 丢弃)--> resolved map
   --(buildTree 只列已有键)--> UI 高级树 --(无法新增)--> 用户看不到/改不了
```

目标（presence 显式、字段完整）：

```
extractor conf --(十进制 + null 占位)--> HOCON(含每一项) --(Number/null 保留)--> resolved map
   --(权威全集 + null 占位)--> UI 完整列出 --(可编辑/可写回)--> 导出/运行
```

不变量：
- 攻击关键路径与 native 解析不变。
- v2 的「键是否出现」语义保持不变；`null` 与「提供了 0」的区分由决策点 D3 统一定义。

## 兼容性与回滚

- 旧内置 profile 仍可解析（新增字段均为可选/null）。
- 回滚：各文件独立，可按文件 revert；extractor 输出格式变更只影响新生成的 conf。

## 验证矩阵

| 批次 | 命令 | 预期 |
| --- | --- | --- |
| extractor | `cargo test --release --manifest-path tools/extract_rs/Cargo.toml` | 全过；新增「conf 数字全为十进制」「字段完整含 null」用例 |
| Kotlin 单元 | `./gradlew :app:testDebugUnitTest` | 全过；新增「null 字段在 UI 树出现」「十进制写入 binary」用例 |
| NDK/lint（若触及 Kotlin 无关） | `make -C src native-host-tests` + `make -C src lint-tidy` | 不变（未触 native） |
| 端到端 | `./gradlew exportKernelProfiles` | 导出的 `.bin` 含 `kernel_phys_load` 等字段 |
| 真机 | 门禁记录（PROFILE-* 格式） | 加载新 profile，route 命中、写验证通过 |

## 明确保留

- `src/core/`、`kernelsnitch/`、`LegacyProfileConverter.kt` 的 v1 转换、GLK1 wire 版本。
- 现有内置 profile 的正确数值与 `include` 结构（除非决策点选择「展开为完整字段」）。

## 决策记录（已确认）

- **D1 = 完整加载含公共项**：UI 编辑器列出当前 route 几何 + fallback + 公共几何
  （`task_struct`/`cred`/`offset`/`kernel`/`kernelsnitch`），缺失显示 `null` 可填。
- **D2 = execution 放一般编辑页**：`execution.*` 在「参数覆盖」（`ProfileOverrideScreen`）完整加载，
  不进「高级参数覆盖」树；不内联进设备 profile HOCON。
- **D3 = A**：`null` = 键不出现在 GLK1 binary（native 读作「未提供」）。HOCON/UI 保留 `null` 仅占位，
  不改变 wire 的 presence 语义，也不引入 presence 位图。
- **D4**：46 份内置 profile 逐份手工补 `null`。
- **D5 = 可执行 branch**：extractor 只输出被选中的可执行 route branch；有 fallback 时连同
  fallback branch 的 geometry 一并输出，`fallback.to` 写实际 route。

### GLK1 v2 的性质（D3=A 的前提）

GLK1 v2 是 Kotlin↔native 的**传输（wire）格式**，也是内置预编译 profile 的载体
（`exportKernelProfiles` 产出 `.bin`，native `--load-prebuilt-profile`）。它**不是**用户编辑的保存格式
（保存为 HOCON/preferences），也**不是**执行格式（native 解析后转为内存 `kernel_offsets`）。因此 D3=A
不改动 GLK1 字节格式，只改 HOCON/UI/extractor 三层。

## 5.15 multicast→select fallback（本批不做，deferred）

> 状态：按用户决定推迟到后续批次；实现前需先解决下方“阻塞项 1”。本批不触碰
> `route_policy.hpp`，extractor 也不输出 fallback branch。

## 5.15 multicast→select fallback（设计记录）

需求：5.15 设备同时具备 mcast 几何与可推导的 pselect 时，主用 multicast，失败回退 select。

- 现状：`run_route_policy` 的 fallback 分支受 `Policy::allows_fallback` 约束
  （`route_policy.hpp:237`）；`SelectPolicy`/`MulticastPolicy` 均为 `false`（`:57`/`:147`），
  只有 `TcpPolicy` 为 `true`（`:93`）。extractor 也把 fallback 硬编码为 `to="none"`。
- 计划改动：`MulticastPolicy::allows_fallback = true`；extractor 在有 pselect 时输出
  `fallback.to="select_stack"` + `fallback.route.select_stack.waiter_shift`。
- **待解决的设计问题（实现前必须定）**：fallback 只切换 `run()`，但 backend 的 W2/W3 能力仍按
  **主 policy** 编译（`multicast=true`、`w2_fast_repair=true`，其 hook 定义在 `MulticastPolicy`）。
  回退到 select 后这些 capability/hook 是否仍成立？两种处理：
  - (a) 仅允许 multicast 在特定 clean 失败点回退，且回退后不进入 multicast 专属 W2 快修；
  - (b) 为 fallback 增加独立 capability 判定，禁止跨 primitive 的 W2/W3 复用。
  须在专节补充控制流图与不变量后再实现。
- 验证门槛（因触及攻击路径，按 AGENTS）：`cmp_disasm` 8 函数 + 冷机真机门禁 + 归档。
- 目前**待确认**：是否先只做 extractor/UI/HOCON 三项（不触 native），把 native fallback 作为独立
  批次，待上述设计问题定稿后再改 `route_policy.hpp`。

## 已知阻塞项（实现前必须定）

1. **native fallback 与 backend 的 capability 错配**：`Pipeline<F,B,M>` 编译期固定主 middleware，
   `retry_write_stage`/`w1`/`w1_scratch_repair` 按 `M::multicast`、`M::w2_fast_repair` 分支
   （`cve_2026_43499_backend.cpp:73/83/346/393`）。multicast 主 route 在 `run_route` 内 fallback 到
   select 后，backend 仍按 multicast 编译分支执行 → 语义错配。要安全打开 `MulticastPolicy.allows_fallback`，
   需让 backend 按**运行时实际使用的 route** 分支（route-aware），或限制回退点。属攻击路径改动。
2. **（已澄清，非阻塞）** `route.tcp_zerocopy.compact_waiter` 经
   `ProfileResolver.nativeValue` 的 branchField 映射落到 native `misc.compact_waiter`
   （`binary.cpp:113` 的 `OPT("compact_waiter", misc.compact_waiter)`），与 6.1 内置 profile 一致，
   无冲突。route HOCON 字段全集 = `tcp_zerocopy.{compact_waiter}`、
   `select_stack.{waiter_shift}`、`multicast_waiter.{waiter_off,buffer_size,task_offset,lock_offset,compact_waiter}`。

## 进度

- [x] 决策点 D1–D5 确认
- [x] extractor `kernel_phys_load` 改十进制（`report.rs`，测试通过）
- [x] extractor 完整字段（route + task/cred/offset/kernelsnitch 全字段，缺失 `null`）
- [x] 内置 46 份 profile 补全（顶层 `kernel_phys_load`、`offset.empty_zero_page`、
      `cred.usage_offset`；共享 `credential-6x.conf`/`kernelsnitch-6x.conf` 补全）
- [x] UI：`GeneralPaths` 扩展为 execution 全字段（一般页）；高级树 `buildTree` 跳过
      `execution` 并按权威全集补 `null`（`completeProfileFields`）
- [x] 写入/导出保留 null（D3=A：OPT 字段缺失不写键，PLAIN 字段无 null 概念，现状已满足）
- [ ] native：route-aware fallback（deferred，见阻塞项 1）
- [x] 主机验证：`cargo test`（34）、`:app:testDebugUnitTest`、`:profile-core:test` 全绿
- [ ] 真机门禁（改动未触 native 攻击路径，但 profile 字段完整性需真机确认一次）
