# schema 设计（profile 与 wire 各一套，文档合为一处）

- **两个 schema 文件，一份设计文档**：`profile/profile-schema.json`（人写侧，定义键）／`profile/wire-schema.json`（执行侧，投影）；本文件是共同设计文档
- **权威**：schema 人工维护；工具只读，禁止反推或改写；变更走 AGENTS §2 的 L 级流程
- **示例**：`profile/examples/43499-multicast.profile.json`（人写侧，用户 2026-10-10 修改定形）与 `profile/examples/43499-multicast.wire.json`（执行侧，由其转出）
- **合并沿革**：原 `profile-schema-design.md` 与 `steps-options-and-two-schemas.md` 合并于此

## 一、形态（以用户修改后的 43499 示例为准）

- 顶层
  - `schema` — 标签：profile 文档 `GLKv3_profile`／**wire 文档 `GLKv3_wire`**（版本仍是 3；加载期按标签判路）
  - `release` — 内核 release 串，选型键
  - `device` — 人类可读设备名（如 `Sony Xperia 1 V`）
  - `steps` — 数组；要跑什么、每步的设置声明与代码
  - `backend` — 对象；静态参数，按 backend id 与功能分组
- `steps[]` 每项
  - `key` — 显示/选择名（示例即「backend 与 route 的人读名」，如 `CVE-2026-43499 multicast waiter`）
  - `options[]` — 该步的设置声明（Kotlin 读它渲染开关）
  - `code` — 该步的 Lua 源码，base64 承载
- `options[]` 每项
  - `key` — 稳定标识符（也是 `settings` 的键、脚本侧 `cfg.settings.<key>`）
  - `input` — 类型名数组，如 `["number","number"]`（核对）、`["bool"]`
  - `default` — 默认取值数组（与 `input` 等长）
- `backend.<id>` 下的功能分组
  - `execution.heap.*` — 7 键：`kernelsnitch_collisions` · `kernelsnitch_timeout_ms` · `prepare_max_attempts` · `prepare_timeout_ms` · `snitch_round_wait_us` · `snitch_threads` · `snitch_wake_rounds`
  - `execution.race.*` — 4 键：`route_wait_ms` · `route_done_timeout_ms` · `setup_settle_us` · `state_poll_interval_us`
  - `execution.stages.*` — 8 键：`w1_attempts` · `w1_settle_us` · `w1_scratch_repair_attempts` · `w2_attempts` · `w2_settle_us` · `w3_chain_rounds` · `w3_attempts` · `w3_settle_us`
  - `execution.handoff.*` — 5 键：`pre_dispatch_settle_ms` · `module_poll_attempts` · `module_poll_interval_ms` · `enforce_poll_attempts` · `enforce_poll_interval_ms`
  - `abi.task_struct.*` — 15 键：`prio` · `normal_prio` · `sched_task_group` · `pi_lock` · `pi_waiters` · `pi_top_task` · `pi_blocked_on` · `pid` · `tgid` · `atomic_flags` · `real_cred` · `cred` · `comm` · `tasks` · `seccomp`
  - `abi.cred.*` — 7 键：`caps_offset` · `usage_offset` · `ref_count` · `ref0_offset` · `ref1_offset` · `ref2_offset` · `ref3_offset`
  - `abi.kernel.*` — 2 键：`kernel_phys_load` · `kernel_phys_offset`
  - `abi.offset.*` — 5 键：`empty_zero_page` · `init_cred` · `init_task` · `root_task_group` · `security_hook_heads`
- `backend.<id>.route.<route>` 下再分组
  - `abi.*` — 结构内偏移（multicast_waiter：`buffer_size` · `lock_offset` · `task_offset` · `waiter_off`）
  - `execution.*` — 循环参数（multicast_waiter：`arm_hold` · `arm_sequence` · `attempts`）
- 取值记法（两条，硬规则）
  - 地址/偏移 ⇒ `0x` 前缀十六进制字符串（schema 类型 `hex-u64`）
  - 计数/时长 ⇒ JSON number（schema 类型 `u32`）

## 二、层次与必填

- `kind=cross`（路径：顶层与 `steps`）— `required: true`
- `kind=backend`（路径：`backend.<owner>.…`）— `required_when` = 选中了含该 backend 的 step
- `kind=route`（路径：`backend.<owner>.route.<route>.…`）— `required_when` = 选中了该 route 的 step
- 生成期机械可检：每层键的 `path` 必须以其前缀开头；实测 43499 示例 70 个叶子**全部被声明**（零未声明）

## 三、三种可选形式（没有第四种）

- ① `required: true` — 数据源必须写；缺失 ⇒ 生成期具名拒绝
- ② 省略 `required` + 写 `default` — 生成期补进文档；默认值只住 schema 这一处
- ③ 省略 `required` 且无 `default` — 文档里可能没有；脚本显式判 `cfg.settings.x == nil`
- 禁止静默零填充（历史隐患 A 的根）

## 四、profile 侧 / wire 侧

- **profile 侧（人写）**：顶层 `schema`（`GLKv3_profile`）· `release` · `device` · `steps[]` · `backend{}`；`steps[]` 每项 = `key` · `options[]`（声明：`key` / `input`＝类型数组 / `default`＝默认数组）· `code`（base64）；**不含 `shizuku`**（系统级，见 wire）
- **wire 侧（本次运行）**：顶层与 profile 同构，外加两项
  - `shizuku` — bool，**系统级设定**，只出现在 wire（profile 不写）；语义：本次运行是否走 Shizuku 通道
  - `step` — **单数**，本次运行**选定的那一步**：`key` · `data[]`（取值：`key` + `input`＝值）· `code`
  - `handoff` — 交接**目标**：`type` ∈ {`lkm`, `script`} + `data`（**一个或多个**载荷位置）；与 `backend.<id>.execution.handoff.*`（交接**时序参数**）分工不同
    - Kotlin 侧可选择 5 种，**最终装配为两类**：`default_kernelsu`／`other_manager`／`custom_lkm` → `lkm`；`custom_command`／`custom_script` → `script`
  - `backend{}` — 投影 profile 的 backend（结构同 profile，默认已补齐）
- 绑定关系：profile 的 `options[]` 是**声明**（`key`/`input`＝类型/`default`），wire 的 `step.data[]` 是**取值**（`key`/`value`）（同 `key`）
- 身份：JCS（RFC 8785）canonical + sha256；每载荷另带 sha256
- 关系（用户裁定）：**profile ⊂ wire**；wire 另含「只有 app 内用户能定的参数」（`shizuku` · `step` 及其取值 · `handoff`）；**允许删无人读取项，但没必要 ⇒ 不裁剪**；键语义只写一次（profile 定义，wire `config_keys.source` 指过去）

## 五、边界

- 不是编程语言：无算术/赋值/模板/include；选择由 `steps[].key` 表达（不再引入谓词语言）
- 不做跨键不变式（如 plan 与文档一致）——留在生成期少量具名代码里
- 不承载运行时密钥：SA/SPI/端口永不进 schema 或数据文档（走会话帧，用后清零）
- 不自举：schema 合法性由生成期检查（路径规则、`required`/`default` 互斥、引用完整性）
- 配套设施不在本次范围：读取器／校验器／生成器均未写

## 六、连带影响

- manifest 的 `doc` 列随 UI 禁用失去消费者，恢复时重定
- `FieldLabelsManifestAgreementTest` 等在禁用清单里 ⇒ 重建 manifest 时一并重建对拍

## 七、待裁

1. ~~`label`~~ — 由 step/option 的 `key` 兼任（要多语言显示名再加）。
2. ~~`cpu_core_pair` 类型~~ — 示例定为 `input:[number,number]`＋`default:[0,1]`。
3. ~~两套 schema 关系~~ — **已定**（见 §四）。
4. `settings` 取值持久化 — app 私有存储 ／ 写回 profile 文件（未定）。
5. **载荷字节的通道** — `handoff.data[]` 只是位置；字节走 stdin／base64（沿用 framing）。
6. schema 文件格式 — JSON（现用）⇒ 待你确认。
7. ~~`input` 同名异义~~ — **已关**：wire 用 `step.data[].value`（你改定）。
8. **shizuku 的语义边界** — 已定为系统级、随 wire 传入；未定的是「谁能改它」（用户开关 / 系统检测 / 两者）。
9. **`handoff.selection` 是否随 wire 传入** — Kotlin 侧的选择要不要写进 wire（供显示与追溯）。
10. ~~`handoff.type` 取值集合~~ — **已定**：`lkm` 或 `script`（5 选项装配为两类，见 §四）。

## 八、与活跃计划的关系

- [config-wire-redesign-plan.md](config-wire-redesign-plan.md) §3 — 本文件是其 schema 细节（裁定不重复）
- [input-parameters-inventory.md](input-parameters-inventory.md) — 参数总清单（含尚未进 schema 的运行期/调用面）
- `docs/profile/PROFILE_SCHEMA.md`（HOCON 时代权威）— 待退场（P3）
- `core/src/profile/key_catalog.hpp`（C++ 键表）— 人工撰写进 schema 后删表（先建后删）
