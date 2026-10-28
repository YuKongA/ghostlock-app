# 审查：98260 R1 的参数化 / 通用化 / native 无状态

> 2026-10-28。级别：只读审查 + 低风险修复。对象：R1 后的 98260 实现（app/service/payload/JNI/共享结构）。

## 0. 总判

| 维度 | 结论 | 阻断项 |
|---|---|---|
| **参数化** | 方向正确（26 数值 + 4 字符串段跨端单一权威、fail-closed），但发现 **3 处缺口**：base 窗口硬编码、`slide_max/base_min/max` 只校验不使用、tracefs 根路径写死。前两项已修，第三项待修。 | 无（P-01/P-02 已修；P-03 重要） |
| **通用化** | runner/log/victim/finish 已按公用结构落地；发现 **1 处推测性扩展**（已删）、**2 处双权威**（已修 1、待修 1）、若干可复用但可接受的重复。 | 无（G-06 重要） |
| **native 无状态** | JNI DSO 除共享 `gl_log` fd 外无业务全局；payload 全局均为**一次性进程的 run-local 状态**（`P` 启动期写一次后只读）。但存在**死 JNI 接口**与**缺成文例外**两个问题。 | 无阻断；P-04/S-04 重要 |

## 1. 参数化（P）

| 编号 | 严重度 | 位置 | 问题 | 处置 |
|---|---|---|---|---|
| **P-01** | **重要（已修）** | `timer.c` plant 调用处（原） | base 窗口硬编码 `0xffffff0000000000..0xffffffc000000000`，绕过 profile 的 `base_min/base_max` 单一权威 | 改为 `BASE_MIN/BASE_MAX` 宏（`params.h`：`(P.base_min)/(P.base_max)`）；plant/calibration 候选与最终 `g_base` 均按窗口过滤 |
| **P-02** | **重要（已修）** | `params.h`/`timer.c` | `slide_max`、`base_min`、`base_max` 只被解析/校验，**exploit 未使用**（死参数） | 新增 `SLIDE_MAX/BASE_MIN/BASE_MAX` 宏；`g_slide > SLIDE_MAX` ⇒ 具名失败（cleanup T 后由上层 park）；`calibrate_base`/`plant_base` 投票前过滤窗口；`g_base` 校准后复检窗口 |
| **P-03** | 重要（待修） | `timer.c:78` `#define TR "/sys/kernel/tracing"` | tracefs 根路径写死；Android 上亦有 `/sys/kernel/debug/tracing` 挂载形态，未匹配则 preflight 直接 fail-closed（非攻击错误，但环境适配差） | 建议：C 启动期探测两候选（可写优先），失败才 preflight fail；或 profile 增 `tracefs_root` 字段（需 schema rev + B1 重跑）；当前不改以控风险 |
| **P-04** | 重要（待修） | `jni98260.c` / `Native98260Jni.kt` | R1 后生产已改公用 victim 启动；`spawnStatic`/`waitExit` 成**死 JNI 面**（含 `TIMEOUT` 常量），增加攻击面且与"单一路径"矛盾 | 清理批删除 C/Kotlin 两处 + 更新 `Cve98260BackendTest` source guard/文档（TD-R1-5） |
| **P-05** | 建议 | `timer.c` | 编译期调参 `NTIMERS/VICTIM_IDX/.../SPRAY_*` 不进 profile；属算法调参、非每构建内核事实，可接受 | 保持；建议在 payload 头注释标注"算法调参，不随内核变化" |
| **P-06** | 建议 | `timer.c` | `page_size` 参数化但 C/Kotlin 都强制 4096（16K 页内核不支持） | 已在主计划记录限制；换 16K 内核需新 profile/schema，不得隐式放宽 |
| **P-07** | 建议 | `timer.c` | `CPU_TRIG 2` 默认值实际总被 `P.cpu` 覆盖（死默认） | 可删默认或注释"无参不可达"；不阻断 |

## 2. 通用化（G）

| 编号 | 严重度 | 位置 | 问题 | 处置 |
|---|---|---|---|---|
| **G-01** | 重要（已修） | `core/victim/VictimCapability.kt` | `EXEC_SCRIPT_NO_KILL` 为推测性 append-only 能力：R1 实际用 `EXEC_SCRIPT` 启动脚本，**无消费者/无协议实现**，违反通用组件"机制先行、不预扩" | 已删除该枚举项（本批新增、未发布，无 ABI 影响）；R2 若需要再带消费者+测试加入 |
| **G-02** | 重要（已修） | `Cve98260RemoteExecutor.kt` vs `Cve98260ExploitPayload.kt` | payload 上限双权威（服务 `MAX_PAYLOAD_BYTES` 与 artifact `MAX_BYTES` 各写一份） | 服务改为引用 `Cve98260ExploitPayload.MAX_BYTES`；删除重复常量 |
| **G-03** | 重要（待修） | `Cve98260FinishScript`/`Cve98260RemoteExecutor`/`Flow98260Impl` | finish 证据 key（`finish_script_sha256`/`finish_exit`/`finish_ok`…）在 3 处字符串字面量重复；payload C/脚本也打印同名键 → 漂移风险 | 建议 `Cve98260FinishKeys` 单点 + C 头镜像 + 对拍测试（需同批改 C） |
| **G-04** | 重要（待修） | `Cve98260ServicePort` 常量 vs AIDL KDoc | 稳定码/状态值 Kotlin 有常量、AIDL 以注释重复；AIDL 无编译期共享 | 建议单测解析 AIDL 文件对拍 Kotlin 常量（与 `PARAM_ORDER` 对拍同法） |
| **G-05** | 重要（已修） | runId 规则 | `Cve98260FinishScript`/`RemoteExecutor`/`FinishAcceptance` 各自 `Regex("[A-Za-z0-9._-]{8,64}")` | 新增 `Cve98260RunId` 单点并三处复用；C 侧 `glk_valid_run_id` 仍跨语言镜像（见 P-05 类问题） |
| **G-06** | 建议 | `ui/backend/BackendFlowRunner.kt` | 43499/98260 两个 runner 的"build→Refused/Built→Flow→异常映射"骨架近似；可抽通用 `FactoryFlowRunner` | 报告建议；避免为两组重复代码引入过度抽象 |
| **G-07** | 建议 | `Cve98260LaunchScript`/`Cve98260FinishScript` | 单引号 shell quote 各一份 | 可提公共 `Cve98260ShellQuote`；低风险 |
| **G-08** | 重要（待修） | `timer.c` finish 阶段已迁出脚本，但 `commit` 语义：finish 脚本路径/marker 路径为常量 | 属后端自有契约，非通用组件污染；保持 | 记录即可 |

## 3. native 无状态（S）

| 编号 | 严重度 | 位置 | 问题/结论 | 处置 |
|---|---|---|---|---|
| **S-01** | 重要（待补记录） | `timer.c` 全局 | payload 全局分三类：① 启动参数 `P`（main 写一次，之后只读）；② exploit run-local（`g_base/g_slide/g_child_task/g_pool/g_rawfd/...`）；③ marker 标志。payload 是**一次性进程**（park/exit 后仅重启清理），无跨 run 持久化；**不违反**"native 不持业务权威"（权威在 app Flow/service 原语） | 在文件头补"run-local/启动期只读"声明；建议静态守卫：payload 不写除日志之外的持久状态文件（现有 `/data/local/tmp` 日志/marker 为有意产物） |
| **S-02** | 重要（已核） | `jni98260.c` | JNI DSO 无可变业务全局；唯一状态是**共享 `gl_log` 导出 fd**（项目日志设施，`attach/detach` 成对；destroy detach） | 保留；文档标注这是唯一 DSO 状态 |
| **S-03** | 重要（待修） | `jni98260.c` 死接口 | `spawnStatic`/`waitExit` 在 R1 后不再生产调用（见 P-04） | 清理批删除；删除前保留源码守卫防误用 |
| **S-04** | 建议 | `Cve98260RemoteExecutor` | service 实例持有 `helperPath/finishRunId/finishScriptSha256` 等 run 事实——**service 会话状态**，不是 native 状态；app Flow 仍是唯一终态权威 | 保持；文档已注 |
| **S-05** | 建议 | `gl_log` | 日志 fd 为跨 DSO 共享状态；一行一次 write、失败不影响攻击（符合 AGENTS §12） | 保持 |

## 4. 已执行的修复（本审查批）

1. `params.h` 新增 `SLIDE_MAX/BASE_MIN/BASE_MAX`；`timer.c` 用 profile 窗口过滤 base 候选、复检 `g_base`、限制 `g_slide`，删除硬编码 base 窗口。
2. 删除推测性 `VictimCapability.EXEC_SCRIPT_NO_KILL`（通用组件不预扩）。
3. payload 上限单一权威（service 引用 `Cve98260ExploitPayload.MAX_BYTES`）。
4. 新增 `Cve98260RunId` 单点；finish 脚本/验收/executor 复用。
5. Port/Flow 中过期 "JNI spawn" KDoc 改为 "公用 victim launcher"。

离线验证：native `externalNativeBuildDebug` **EXIT=0、0 warning**（base/slide 修改后）；Kotlin 侧修复因工作树被在途 43499 文件（`Flow43499Impl.kt` 未解析符号）打断，**未能在最终树复跑**，待外部写入停止后重跑。

## 5. 待办优先级

1. **P-03/P-04**：tracefs 双路径探测（或 profile 字段）、删除死 JNI 接口；同批重跑 B1 与 source guard。
2. **G-03/G-04/G-06/G-07**：finish key/常量/runner/quote 单点化，配静态对拍测试。
3. **S-01 记录**：在 payload 头注释与计划中成文"payload 全局=run-local/启动期只读"的例外说明，避免与"native 无状态"原则的表面冲突。
