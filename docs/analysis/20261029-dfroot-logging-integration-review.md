# DFRoot 43284Glued 通用日志接入设计补充

状态：logger-only 源码接入已落在当前工作树 `app/src/main/cpp/43284glued`，并并入唯一活动计划；外部 DFRoot checkout 未修改。原 design-critic BLOCKED 记录保留为历史；当前批次已通过 host/debug/NDK 构建验证，运行时 logcat/run.log 与真机行为尚未验证。

## 冻结快照与现状

- 工作树：`/Users/nickji/.codex/worktrees/exec-glue-clean/ghostlock-app`；HEAD `b119085915748a940740d7ce919b5a30ed8e9346`，detached。工作区已有大量用户改动/删除及未跟踪文件；不得清理、覆盖或重置。
- 目标源当前 SHA-256：`exp.c afdd68cb6b07fa9b04f93c7d4bd9f8ce0333f4dd2265416959cf9c630d55ac39`；`elf_parser.c 03500a7e5c635d46a92adc87b899a3eef2933c7c27cce3c66128cdd81d5ffb3e`；`bootstrap.c 5b19bfe9d4c632794d52de71a513d1bac2820e0fbd474ce19813913782111ce9`。目标 Glued 文件未跟踪，CMake 为 GhostLock 适配版。另一个 DFRoot checkout 有脏改动，禁止修改；先核验 pristine source copy/manifest，不能把其本地 `df_log.c/.h` 当作上游来源。
- 已有 C logger：`app/src/main/cpp/common/gl_log.{h,c}`。默认阈值 I，提供 V/D/I/W/E API；但 Android 全部用 `ANDROID_LOG_INFO`，文件级别编码为 `V/~/*/-/!`，fd 写入循环多次 `write`，导出行无源文件行号且未规整换行/非 ASCII，尚未完全满足 AGENTS §12 的 `V/D/I/W/E`、ASCII、单行单次写和等级语义。
- `43284glued/CMakeLists.txt` 的生产 ELF 与 M0 fixture 分支分别链接 `log`；生产 `exp.c` 有 57 处 `printf/fprintf`，`elf_parser.c` 有 26 处。App/Shizuku 的 `ExecutableBackendProcess` 合并 stdout/stderr 并由 collector 写 `run.log`；不能假设 system-root bootstrap 的 stdout 进入该文件。
- M0 fixture 的源拷贝、解析器和失败现场与生产源分离；本提案不改 marker fixture，也不重跑已消耗的 A301SO marker retry。最近一次 M0 在 patch 前以 `vendor-tail-read` 失败，未加载 KO/UMH、未执行 Payload/Finish。

## 目标、行为与边界

目标是在 Glued DFRoot 原执行流程中用 GhostLock 通用 logger 替换诊断输出，让默认 I 记录阶段与不可降级错误，D/V 仅提供可控诊断；日志失败始终不改变返回值、重试、清理、触发顺序或 Finish 交接。

1. 先让 `gl_log` 满足项目日志约定：Android priority 精确映射 V/D/I/W/E；fd 行使用 `[级别/启动毫秒] body`，一条日志调用最多一次 `write(2)`，失败丢弃且不影响主流程；输出单行 ASCII，CR/LF/控制字节规整；logcat 仍为 `file:line body`。补现有测试框架内的级别、格式、过滤、单次写和失败隔离覆盖；检查所有现有 `gl_common` 使用者的行为回归。
2. 生产 `43284Glued` 初始化 logger：以 stdout 为导出 fd（`ExecutableBackendProcess` 已将 stdout/stderr 合并并落入 run.log），同时输出 Android logcat；默认 I，Debug 构建可到 D，Release 不低于 I。bootstrap 是独立 system-root helper，仅输出 logcat，不将 stdout 误认为 app run.log。
3. 仅替换已审阅的 DFRoot 诊断点并按语义分级：阶段/结果 I，预期不可用/可恢复 W，失败 E，逐步偏移/分支 D，逐项数据 V。日志不得记录 key/token/秘密；删除或脱敏 SPI 类标识。触发/竞态/忙循环中不新增日志；日志只位于现有诊断点的安全阶段边界，禁止为日志新增等待、分配、同步、格式化或 I/O 到关键时间窗口。
4. 保留 DFRoot pristine source copy 与 manifest/哈希；仅改 Glued 适配副本、`gl_log` 实现/测试、对应 CMake、日志点与日志清单。M0 fixture 分支保持隔离。同步 `docs/analysis/log-levels-and-sites.md` 和 full-process UML 的日志输出路径/进程边界；在实施批次记录来源文件、变更 diff、回滚点。

## CBN 与决定

- 方案 A（选）：复用 `gl_common` 并修正其规则偏差。无新增日志实现；生产 ELF 复用既有 stdout collector 与 logcat，统一级别/格式。成本：修正共享实现并覆盖所有现有使用者；主要风险是改变旧调用者日志外观、时序，以及误把日志插入触发窗口。
- 方案 B：在 Glued 内新增/移植 DFRoot 私有 `df_log` 或继续 `printf`。继续 printf 无统一等级/源位置；私有 logger 会造成并行 API/实现，外部 DFRoot 本地 `df_log` 目前不是 pinned upstream 且处于脏工作树。成本更高，规则漂移风险更大。
- 可测取舍：方案 A 只保留一份 C formatter/级别状态，验证调用只经过 `gl_log_write`；若需要新增第二套 formatter 或把每个触发点加入日志即视为收益反转，退回设计。

## 实现门与验证矩阵

此任务只加日志，不修 DFRoot exploit，也不执行 Payload/Finish。编码前需：design-critic 无阻断；用户批准；确认 pristine 来源副本/manifest；压缩唯一活动计划至配额并将此补充折叠入其中。单批改动，保留用户现有工作。

| 门 | 判据 |
|---|---|
| Baseline | 改动前运行 logger/native 与 `:app:testDebugUnitTest`；记录命令、退出码、XML 测试数及目标文件哈希。 |
| Logger | 级别与 Android priority 精确对应；低于阈值零输出；每条 fd 记录一次 `write`；CR/LF/非 ASCII 不破坏单行 ASCII；无效 fd/写失败不影响调用者。 |
| Native/build | Debug/Release 两变体 NDK 构建；M0 分支保持原源与 marker 解析契约；静态检查生产 DFRoot 无残余裸 printf/fprintf（白名单仅非诊断 ABI 必需项）。 |
| Pipeline | host 测试检查 logcat 与 run.log 的字段/顺序/非零退出路径；进程 stdout collector 仍按行写 run.log；bootstrap 只验证 logcat 接入，不宣称进入 app run.log。 |
| Attack constraints | 人工逐条对照 PI/触发/cleanup/worker 生命周期；比较改前后功能调用/控制流，日志故障注入不改变状态转移；此批不执行 payload。真机攻击门若因规则需另行执行，须另有明确门禁，不用失败过的 M0 retry 充数。 |
| Rollback | 同批生成可执行回滚脚本；在独立副本上回滚后，logger 与上游源 hash/构建行为恢复，生产修改文件保留用于复核，提交前不覆盖用户改动。 |

## 预期改动清单

`app/src/main/cpp/common/gl_log.c`、`gl_log.h` 及现有 native logger 测试；`app/src/main/cpp/43284glued/CMakeLists.txt`、`exp.c`、`elf_parser.c`、`bootstrap.c`；DFRoot pristine source copy/manifest；`docs/analysis/log-levels-and-sites.md`、`docs/development/full-process-uml.md`；必要的日志集成测试。精确调用点清单及退出码会在实施前按冻结快照补齐。本文件是审查补充，不是第二份活动部署计划。

## Design-critic 记录

`2026-10-29 UTC | SHA256=d9c97d593953aa2fdb26f052fe024d34a590fd45ed08befefcf8d8d0c058117c | BLOCKED | 1 阻断/4 重要/1 建议 | 先压缩并合并唯一活动计划至≤16 KiB，再冻结逐调用点安全/敏感性清单、确认单次 write/ASCII 与 bootstrap/logcat 和 ELF/run.log 两条路径后复核。`

## 2026-10-30 logger 接入结果

### 实际改动

- `common/gl_log.{c,h}`：priority 精确映射 V/D/I/W/E；fd 格式 `[级别/启动毫秒] body`；控制字符折叠为空格、非 ASCII 替换为 `?`；每个 fd sink 仅一次 `write(2)`，失败丢弃；logcat 为 `file:line body`。legacy timestamp 参数保留兼容，但输出始终带单调毫秒。
- `43284glued/exp.c` 原 57 个 printf/fprintf 点中 56 个改走 logger；唯一保留直接 stdout 是紧邻 `fork()` 的 `Triggering hook...`。SPI 数值只记配置存在，AES key 和 manager package 不输出。`elf_parser.c` 26 点全部接入；bootstrap 新增 14 个阶段/错误点，root helper 只写 logcat，`late_load_running()` 等待循环无日志。
- `43284glued/CMakeLists.txt` 的生产 executable/bootstrap 链接 `gl_common`；M0 fixture 分支与 `m0/` 源未改。添加 `SOURCE-MANIFEST.json`。逐点级别、字段、进程/sink 和边界审阅：`docs/analysis/dfroot-log-callsite-audit-20261029.tsv`（111 rows）。UML 已补 App/Shizuku run.log 与 system-root logcat 进程边界。

### 来源与 APK 身份

Pristine DFRoot 输入 SHA-256：`exp.c afdd68cb6b07fa9b04f93c7d4bd9f8ce0333f4dd2265416959cf9c630d55ac39`；`elf_parser.c 03500a7e5c635d46a92adc87b899a3eef2933c7c27cce3c66128cdd81d5ffb3e`；`bootstrap.c 5b19bfe9d4c632794d52de71a513d1bac2820e0fbd474ce19813913782111ce9`。修改后 hash 与 pinned revision `49e992ef3c9f7ec03c8efea6e3cf552f7ec6a347` 记于 `43284glued/SOURCE-MANIFEST.json`。外部 DFRoot checkout 未改；该 checkout 本地没有 pinned commit object。

Debug APK `build/app/outputs/apk/debug/GhostLock-v2.0.12(1342)-arm64-v8a-debug.apk`：SHA-256 `37cf44d5be3aa346ad9b72b8b8fb1ab304255ec74e5d4e61ba0a048f9ae892c4`；`aapt` 确认 package `com.ghostlock.app`、version `2.0.12`/1342。APK 内 `lib43284Glued.so` 为 ELF PIE/ET_DYN、1,945,568 B、SHA `c539ff9e5553768bb1f3667878212442aad38f964e09d217fc4624b7e88a9f3a`；`libbootstrap.so` 为 ELF PIE/ET_DYN、19,984 B、SHA `dbe7f35f9924e73d431a1feb150edc968afe5bac297ebf3a2362051e420f0787`；两者均含 `GhostLockNative` 与 `__android_log_write`。这是 APK 静态检查，不是设备日志验证。

### 验证矩阵

| 阶段 | 命令 | 字面结果 | 退出 |
|---|---|---|---|
| BASELINE unit | `./gradlew :app:testDebugUnitTest --no-daemon --console=plain` | `BUILD SUCCESSFUL in 42s`; 33 tasks (6 executed, 27 up-to-date) | 0 |
| BASELINE build | `./gradlew :app:assembleDebug --no-daemon --console=plain` | `BUILD SUCCESSFUL in 8s`; 42 tasks (7 executed, 35 up-to-date) | 0 |
| MODIFIED logger | baseline and current Clang commands in `/tmp/dfroot-logging-integration-20261029/VERIFICATION.txt` | baseline and modified host tests both PASS; modified injects EINTR, partial write and EBADF, asserts one write/no retry, then verifies a later record still writes | 0 |
| MODIFIED debug/native | `./gradlew :app:testDebugUnitTest :app:assembleDebug :app:configureCMakeRelWithDebInfo :app:buildCMakeRelWithDebInfo --no-daemon --console=plain` | `BUILD SUCCESSFUL in 42s`; 54 tasks (13 executed, 41 up-to-date); XML 69 suites / 424 tests / 0 failures / 0 errors / 0 skipped; RelWithDebInfo CMake passed | 0 |
| MODIFIED release packaging | `./gradlew :app:assembleRelease --no-daemon --console=plain` | `BUILD FAILED` at `:app:compileReleaseKotlin`: unresolved `M0_MARKER` in `ExecutableRunEnvironment.kt:22`, `ExecutableShizukuHost.kt:76,97`; preceding `buildCMakeRelWithDebInfo` passed. These pre-existing dirty Kotlin/Gradle files were not edited in this batch. | 1 |

### 边界与未验证项

- 本轮 Debug APK 已部署至 AVD 和 A301SO，设备取回的 `base.apk` SHA 均与构建 APK 相同；AVD 首页 UI 已显示。A301SO 当时处于锁屏，Activity 启动请求返回 `Status: ok`，但手机屏幕仍显示锁屏，故不声称手机 UI 可见验证。未运行 production Glued ELF、KO、UMH、Payload 或 Finish；M0 retry 不重跑。没有设备 logger `logcat` 样例或真实 `run.log` 样例。
- Executable 现在同步写 stdout/run.log 与 logcat；设备上的额外同步 sink 时序影响没有实测。trigger 前固定 `printf` 保留；本批不证明漏洞可用或 supported。
- 事务差异/验证/回滚文件：`/tmp/dfroot-logging-integration-20261029/{DIFF_FILE.patch,VERIFICATION.txt,ROLLBACK.sh}`；`MODIFIED_FILE` 为 `app/src/main/cpp/common/gl_log.c`，保留修改。原始备份在同目录 `baseline/`。

### 2026-10-30 critic follow-up and fault-injection evidence

- 当前唯一活动计划为 16,349 bytes（上限 16,384）；该配额发现按实时文件复核后关闭。Release 仍被既有脏 Kotlin 文件中的 `M0_MARKER` unresolved 阻断；Debug 与 NDK 证据不替代 Release。
- host sink 注入证明 EINTR 和 partial write 都只调用一次 `write`、不重试，之后成功写入不受污染；无效 fd 的 EBADF 被 logger 丢弃。测试二进制退出 0。
- 未关闭项：host 注入不等于攻击状态转移/清理端到端证明；设备上的 ELF `run.log` 与 system-root `logcat` 分离、共享 logger 所有消费者的生命周期/ABI 回归尚无运行时证据。未声称本轮完成真机验证或 exploit 成功。
