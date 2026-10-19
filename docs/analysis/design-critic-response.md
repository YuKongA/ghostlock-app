# design-critic 评审回应（C1–C23）

- 评审记录：2026-10-19T21:59Z，总判=仍需修订，阻断=14。
- 本文件逐条回应；状态只写已改/部分/未改，不改写评审原文。

| 编号 | 状态 | 回应 |
|---|---|---|
| C1 | 未关闭 | raw 直写是用户明确要求“进行 kotlin 直写”后的实现；仍需用户确认是否作为强制例外，并补 CBN/技术债/清理批次。未擅自删除 native 路径。 |
| C2 | 已修 | raw 路径现在要求 `blocks == padded/16 && full != null && mismatch == -1`；`KotlinPageCacheWriter.ok` 要求 `written>0 && verified==written`，并拒绝 `effect != CONFIRMED`。 |
| C3 | 已修 | `PlanClosure` 分别校验 imageEnd→carrierSize、payloadEnd→hookTargetSize；保留原 image refusal 字符串兼容测试。 |
| C4 | 已修 | `EntryPayload.plan(kotlinTarget)` 改用 `KotlinElfPlanner.planEntry` 派生 site/landing/vaddr/payloadMax/displaced；branch 改为 site→landing 直接分支（不再走 +0x90 固定常量）。 |
| C5 | 已修 | payload 内部把 `/dev/df` 改为 `/dev/dz`，恢复轮询同步改为 `/dev/dz`；仍以“payload 首步”为恢复信号，属于待真机验证的时序风险。 |
| C6 | 已修（静态审查） | raw restore 后回读 site 16B 与 464B stage，逐字节比较，结果写入 `finish.noteHookRestored`；native restore 仍会再写一次，需真机确认不覆盖 raw 结论。 |
| C7 | 已修 | `dfrootPayload || staticHook` 且 `rawHandle==0` 时直接 `error("dfroot:raw-sa-missing")`，不再降级到普通载荷写。 |
| C8 | 已修 | Kotlin plan 为 null 时 `hookPlanWords=null`，不再回退 native 固定 plan；planGate 失败停止写/触发。 |
| C9 | 已修（静态审查） | `plan`/`planEntry` 先选 landing，再按 landing 计算容量；移除“tail 不足直接 return null”的前置阻断，零填充候选可达。 |
| C10 | 已修（触发门） | trigger 增加 `planGate.ok && hookWrote && moduleReady` 硬门；模块 precheck 顺序仍未前移，单独登记。 |
| C11 | 已修 | `writeScope` 改为 `com/ghostlock/app/...` 真实路径；新增 `writeScannerCanFail` 负例。 |
| C12 | 已修（marker） | 主开关改为 `DfrootExperiment` 常量；删除 raw/benign 文件写与 6 个 `.gl_*` 诊断 probe 块；剩余 `/data/local/tmp` 路径为既有 carrier/wait 逻辑，另案登记。 |
| C13 | 部分 | `dfroot-parity-review.md` §8 已给出静态入口权威结论；旧段落仍保留为历史，需同构建对照日志后彻底删除。 |
| C14 | 未关闭 | 真机冷机门禁未做；按用户要求放在代码审查后。 |
| C15 | 已修 | 模块幂等改为逐块比对完整 `moduleBytes`；不再只看 ELF 头。 |
| C16 | 部分 | `noteHookApplied` 只在真实写出时记；raw payload 的 verified 块数写入 outcome；finish 行仍可能与 raw restore 账并存。 |
| C17 | 已修（边界） | DFRoot stage 用 `STAGE_SIZE`；`fillPayloadSlots` 下限改为 0x1D8，避免 468B 在 0x1D4 越界；旧 472 布局仍只服务 sentry。 |
| C18 | 复核为误报 | `TBP:2395-2620` 是 raw 分支，`TBP:2805-3010` 是 `else if (kotlinProbe/hookPage)` 的 access 分支；同一调用只走其中一条，不是两次应用。保留两条实现以覆盖两种写后端。 |
| C19 | 已修 | 日志不再写 SPI/端口；finally `closeRawSa()` 关闭 transform/encap、key 清零、rawReady 复位。 |
| C20 | 部分 | 修了 `layout.exec!!` NPE；零填充候选仍只排除 relocation，未限制 `.text`/SHT_PROGBITS。 |
| C21 | 已修 | 夹具已入库；新增 `RawEspWriterIvTest`，用固定 AES-256-ECB 向量锁定 IV 数学。 |
| C22 | 已修（诊断） | 删除 `writeChunk`/`<vector>`；新增 `NativeRawEsp.lastError()`，失败阶段+errno 写入 thread-local；`SPLICE_F_GIFT` 与 DFRoot 一致保留。 |
| C23 | 已修（carrier） | UML §8 已补；写入顺序统一；payload 的 ko_target 由 `carrier.path` 在 0x88/64B 处注入，不再写死 vendor 路径。 |

## C1 强制例外登记（CBN）

- 用户指令：在 native landing 无法把 crash_dump64 写入落地后，明确要求“进行 kotlin 直写”；
- 备选：继续修 native `landing_of_door`/`device_make_landing`；
- 收益：绕过 native 单 session/目标绑定，Kotlin 直接指定目标与偏移；
- 边际成本：新增 220 行 JNI syscall + 第二套 `RawEspWriter`，形成两条写权威；
- 翻转条件：native landing 能稳定按目标写入并回读，则回退 raw 路径；
- 技术债：`native_raw_esp.cpp`、`RawEspWriter`、`DfrootLibcxx`、`native_payload_probe` 全部登记；清理批次 = native 修复后同批删除；
- 当前状态：用户强制例外，未删除 native 路径，也未把 raw 路径设为默认生产。

## 门禁

- `:app:testDebugUnitTest` PASS（155 tests）；`:app:assembleDebug` PASS；
- 未做真机门禁；未关闭的阻断项仍使总判保持“不得批准”。

## 下一步

1. 关闭 C10/C12/C13/C14/C15/C16/C17/C18/C19/C21/C22；
2. design-critic 复评快照；
3. 真机冷机门禁与归档。

