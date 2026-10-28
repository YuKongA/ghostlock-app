# L 设计计划：CVE-2026-98260 backend（POSIX CPU timer UAF / fake-cred → KSU late-load）

> 级别 **L**（新 backend + 攻击关键路径 + 跨端契约）。日期 2026-10-28。作者：Lead。
> 快照：`dev-nocpp` HEAD `a9af5482`「2.0.11」+ 工作树（dirty；含在途 task-32；in-flight sha 见附录 B §J）。
> 上游分析：[98260-upstream-analysis.md](../analysis/98260-upstream-analysis.md)（`YuKongA/CVE-2026-98260` @ `ad1f4e7`，MIT）。
> 细节：附录 A [98260-backend-plan-annex.md](98260-backend-plan-annex.md)（critic 回应/启动/状态/预检/marker/参数协议）；附录 B [98260-backend-plan-annex-b.md](98260-backend-plan-annex-b.md)（CBN/改动清单/测试/真机门禁/债）。
> **2026-10-28T09:20 用户裁定（D2 修订）**：执行通道改为「**Shizuku UserService 承载 Flow（app 包代码，shell 域进程）+ JNI 控制独立 payload**」；详见 [delta](98260-backend-plan-delta-userservice.md)（取代 D2/§5/附录 A §C/附录 B §B-D2）。**payload 形态与 B1 不变。**
> 依据：AGENTS.md §2/§8/§9；`design-review-principles.md` v0.2；ADR-0022（Flow 执行权威）；`软件工程守则.md`（最高思想）。
> **2026-10-28 结构适配补编（v2.1，用户已批）**：[98260-project-structure-adaptation-plan.md](98260-project-structure-adaptation-plan.md)（五块公用结构；R1a–R1d；R2 另立；细则见补编）。

## 1. 在办计划判定（2026-10-10 用户裁定：计划前先查活跃计划）

- 已读：`REMAINING-PLAN.md`、`MASTER-PLAN.md`（旧 C++，不覆盖）、`execution-architecture.md`（权威让位 ADR-0022）、`adding-a-component.md`（C++ 组件树已删⇒不适用；standards 指针 TD-6）。
- 结论：**无 98260 相关计划**；本计划为独立新 backend 批次，不重复、不取代在办计划。与在途 task-32 有文件接触点（附录 B §J），依赖其落地（B3 `blocked_by`）。

## 2. 目标与不在范围

**目标**：新增第 4 个 backend「CVE-2026-98260」，把上游 timer exploit 以**独立 shell 域 helper** 接入 app：选择/配置/就绪/运行/结果/重启安全状态机闭环；离线门禁绿；真机门禁 pending。

**不在范围**：不改 43499/43284 攻击语义与生命周期；不改 finish 收尾链；不改主 assets profile 与 wire/schema；不修在途 43499 JNI 缺失入口（交回在途批次）；不用 `newProcess`（UserService 为唯一通道，见 delta）；不 commit/push。

## 3. 上游结论 → 形态约束（详见分析文档）

1. 硬依赖 **shell 域**（uid 2000/`u:r:shell:s0`）+ **tracefs** + 独立进程语义（`T:1435-1439` re-exec、`T:1631-1632` argv0）⇒ **in-process JNI 结构性不可行**。
2. 唯一形态：**shell 域启动的独立静态 helper**；app 只 install/launch/解析 marker/验证/"必须重启"状态。
3. 触发后不可逆：**绝不 kill/timeout/reap**（`T:28-31,813-819`）；失败即 park，重启是唯一清理。
4. 终局 = **fake cred → KSU late-load**；real-cred 链仅保留为 Debug 诊断（`T:1770-1774`：仅有 held 失败 plant 时才 park）。

## 4. 关键决策（完整 CBN 见附录 B §B）

| 决策 | 结论 | 一句话理由 |
|---|---|---|
| **D1 宿主形态** | A 独立 helper（shell 启动） | 4/4 前提（域/tracefs/park/exec 语义）只有 A 满足；JNI 4 项冲突（域/tracefs/exec 语义/park；分析 §3）；C 依赖未过门禁的 43499 |
| **D2 启动通道（09:20 修订）** | **B′ Shizuku UserService（AIDL）+ JNI 控制 payload** | app 包里的 Flow 代码由 UserService 在 **shell 域独立进程**执行，native 交互仍走 JNI；取代 `newProcess`；详见 delta §2。未验证：UserService 内 `System.loadLibrary`/`posix_spawn` 策略（TD-14/15）⇒ G8/G9 真机门禁 |
| **D3 参数权威** | A debug-only HOCON → 严格解析 → argv | 执行层目标常量 0 处；release fail-closed。**越权 AGENTS §4（配置权威=profile）**，用户批准后按强制确认留沿革+TD-2，真机门禁后归一化主 registry |
| **D4 终局** | A fake cred→内置 late-load | 达成 app 目的；real 链 bset caps 无法 late-load；KSU 判据独立于日志（`/proc/modules`+`su` 上下文） |
| **D5 终态表达** | A 保持 4 态 + 具名 Failed + 独立重启警示条 | 不新增第 5 态；「不确定」映射到 `cve98260:park-unreaped / poll-window-timeout / launch-unconfirmed / armed-abnormal-exit`，禁止伪装成普通失败；**BootSafetyStore 唯一权威、evidence 仅镜像**；**成功(PARKED)也置 must_reboot=true**（附录 A §E） |
| **D6 失败/重启** | A 单次 armed 运行 + BootSafetyStore；中止不 kill | armed 后二次运行=panic 风险；`launch_pending` 先落盘（app 中途死亡也不失闸）；`/data/local/tmp/glk98260.run` journal 关闭清数据/卸载后的同 boot 再 armed 窗口；回滚禁清闸（附录 A §D/§N） |
| **D7 回执** | A stdout marker + 日志轮询 | 无 JNI、可离线单测；marker append-only（附录 A §G） |
| **D8 身份权威** | A `BackendId` 唯一；`BackendSpec.id`；dispatcher 穷尽 `when` | 身份权威唯一；`cve`=显示/持久化键、`index`=UI 选择索引（均非身份）；编译期漏项即红 |
| **D9 文档权威** | A 执行结构进 `execution-architecture.md`；UML 按自身 §9 只更新 IPO/状态图并互链 | 避免第二处执行结构权威（B3） |

## 5. 目标架构

> **执行结构图已被 [delta §1](98260-backend-plan-delta-userservice.md) 取代**（三层：app Flow 唯一编排 / UserService+JNI / 独立 payload）。旧 ShellChannel 图仅沿革，不再复制。

**运行态（进程拓扑/步骤以 delta §1/§3 为准）**：`Idle → app Flow validate(app 初筛 F9/F10/F12) → prepare(service F1–F8/F11 + journal installed) → start(service journal launched→JNI spawnStatic→pid/proc 存活) → poll(进程内只轮询 /proc+readLog) → verify(service) → acceptance(先落 store must_reboot=true) → {Ok | Failed(具名) | Refused}`。**任何路径都不向 helper 发信号**；`pollWindowTimeout` 仅指等待窗口，不含 kill。预检=单一纯函数求值器（载体 delta §F；app 初筛快照 + service prepare 重跑）；boot_id 未知 ⇒ NotReady，**不写 must_reboot**。

## 6. 参数与配置

- profile：`app/src/debug/assets/profile/98260/debug-plz110.conf`（debug-only；release 无 profile ⇒ NotReady）。
- 唯一权威：C 头 `gl98260/params.h::struct glk98260_params`（名/序/型）；Kotlin `PARAM_ORDER` 为镜像，单测解析 C 头逐字段对拍（**不只顺序**）；argv 协议含 re-exec 传播（附录 A §H）。
- 字段：`release,kernel_major,page_size,cpu,slab_order,sockbuf,slide_max,base_min,base_max,static{6},sizes{5},offsets{3},symbols{4},caps{2},manager_package,kmi,late_load_args,ksud_fallbacks,tracefs_path`。数值不入本文。
- 调参常量（`NTIMERS/VICTIM_IDX/PREFILL_TIMERS/VMA_COUNT/LOCKERS/SPRAY_*/CPU_TRIG`）**保留编译期**于 params.h：部分被上游用作静态数组尺寸（`T:933-936,1049-1053,1106`），其余改运行期会把改动面扩进攻击代码（C1/D7 权衡）；换调参需重编 helper（随 APK，可接受）。**附录 A §H 的 argv 不含调参字段（单一权威=编译期）。**

## 7. 改动清单（未列入即冻结；完整表与行数预算见附录 B §J）

**新增**（09:20 修订：ShellChannel 相关项由 delta §4 取代；以下为本计划基线+delta 增量）：`app/src/main/cpp/gl98260/{timer.c,params.h,params.c,markers.h,jni98260.c,jni_ops.*}`；`ui/backend`/`jni`/`cve98260/service|client`/AIDL/manifest；`core/flow/FlowProgress.kt`；`data/cve98260/{Cve98260ProfileConfig,BootSafetyStore,Cve98260ReadinessProbe}.kt`；`backend/cve98260/{profile,flow}/**`（Markers、Profile、Flow98260Request/Impl）；**AIDL/service/client/JNI/RemoteExecutor 精确清单见 delta §4**；`ui/screen/module/backends/{Cve98260RequestFactory,BackendCve202698260}.kt`；`ui/backend/BackendRunUiState.kt`；`app/src/debug/assets/profile/98260/debug-plz110.conf`；`core/preflight/Cve98260Preflight.kt`（单一预检求值器，中性层；data 探针与 backend Flow 均只依赖 core）；测试（附录 B §K）；决策/评审记录=`docs/analysis/architecture-review-log.md` 评审行+本计划/附录（不新增 ADR）。

**修改（机械/最小）**：`backend/BackendId.kt`(+CVE_98260)；`BackendSpec.kt`(+`id: BackendId`、+`abortable: Boolean get()=true`、`Backends.all`=[43284,98260,43499,Custom]（98260 插 43284 后；默认 index 0 不变，已存选择按 cve 恢复）)；`BackendId.LUA ↔ BackendCustom` 显式映射入 1:1 测试（`[CVE_43284,CVE_98260,CVE_43499,LUA]`）；3 个**既有** spec 实现各 +1 行 id（+新增 98260 `abortable=false`）；`BackendSessionStore.kt`/`HomeMaterial.kt`/`HomeMiuix.kt`：cve 仅作**显示键/持久化键**（身份权威=`BackendId`），清单点名并加 1:1 映射测试；`BackendRunGate.kt`（状态迁移+中性类型+按 BackendId 穷尽 dispatch）；`BackendRunViewModel.kt`/`RunPage.kt`×2（类型改名+abortable+重启警示+progress 注入）；`GhostLockApp.kt`（readiness 探针 attach）；`BackendPage.kt`（选中 98260 时 refresh 探针）；`app/src/main/cpp/CMakeLists.txt`（+payload ET_EXEC 与 +JNI SHARED 两目标；属性/先例/未验证项见附录 B §J 与 delta §4）；`strings.xml`+`values-zh-rCN`；`FlowRequestTest.kt`+1；`REMAINING-PLAN.md` 索引一行；`architecture-review-log.md` 评审一行；`execution-architecture.md` 98260 结构图；`full-process-uml.md` 按 §9 只更新 IPO/状态图并互链。

**明确不动**：`cve43499/**`、`cve43284/**`、`FinishHandoffAdapter/FinishExecution/FinishAcceptance`、主 `assets/profile/**`、`gl43499/**`、`gl43284/**`、`glgeneral/**`、`kernelsnitch/`。在途 task-32 改动原地保留。

## 8. 行为差异

1. 下拉框/主页 badge 出现「CVE-2026-98260」（排 43284 后）；NotReady 原因具名（Shizuku/身份/profile/must_reboot/未知）。
2. 满足条件时运行页可执行：progress 逐行显示 marker；终态 Ok（KSU 判据+独立验证；**成功=漏洞利用成功、仍需重启清理**，PARKED 持有 fake cred 页）/ Failed（具名）/ Refused；成功或不确定后常驻「必须重启」警示并拒绝同 boot 再跑。
3. 选中 98260 时中止无效（`abortable=false`，不改状态、不 kill）；43499/43284/Lua/Custom 行为、profile/wire、native DSO 加载列表不变（helper 不 `System.loadLibrary`）。

## 9. 验证计划（D1：黑盒/白盒 + 极端输入；D3：真机唯一判据）

**B0 基线**：先 `./gradlew :app:testDebugUnitTest --offline` 与 `:app:externalNativeBuildDebug --offline`，EXIT=0；失败数读 `build/app/test-results/**/*.xml` 三桶；红项先归属在途 task-32，不混入本批判据。

**离线门禁**：`externalNativeBuildDebug`/`assembleDebug` EXIT=0 且 98260 target **零告警**；产物 `readelf`（`$ANDROID_NDK_HOME` 或 `local.properties` 的 `ndk.dir` 下 `toolchains/llvm/prebuilt/<host>/bin/llvm-readelf`；**不读 local.properties 其它键**）：aarch64 static ET_EXEC、16KB、无意外依赖；`testDebugUnitTest` EXIT=0；APK `unzip -l` 含 `lib/arm64-v8a/libghostlock98260_helper.so` 并记录路径/mtime/字节/sha256（`shasum -a 256` / `openssl dgst -sha256`，跨平台；**仓内 10-27 旧 APK 不作本次产物**）。

**白盒矩阵**：完整矩阵见附录 B §K；覆盖 profile 极端值/跨端参数对拍/markers/Flow 预检与终态/取消与 one-shot/「不 kill」拆分断言（app 侧命令集无 destroy/信号，JNI 无 kill API）/BootSafetyStore/readiness 矩阵/注册表 1:1/预检一致性/install 身份/native 源无**目标内核特定**绝对地址/数值（通用 VA 窗口 allowlist）。

**真机门禁**（本会话 `adb devices` 为空 ⇒ **pending**；矩阵命令/预期/归档见附录 B §L）：冷机 PLZ110/`6.12.69-android16-6`、KSU 未加载、Shizuku 已授权；install/launch 存活；ENV→ARMED→SELINUX_OFF→…→KSU_LOADED→PARKED；独立验证；app force-stop 生存（破坏性，先获批）；重启恢复；≥3 冷机含失败/panic 归档；`bootreason != kernel_panic`。**未过真机不得标 supported/不得宣称可用。**

## 10. 安全规则

S1 触发后绝不 kill/timeout/reap/清后台（本计划所有 timeout 均指等待窗口，不含向 helper 发信号；helper 内部 victim kill/reap 属攻击语义、逐条列明）；S2 `parking without reap`/超时/异常/launch 未确认 ⇒ `must_reboot`（BootSafetyStore 唯一权威、evidence 镜像；持久、按 boot_id 失效），Flow 退出前先落盘；S3 同一 boot 最多一次 armed 运行（清数据/卸载窗口由 `/data/local/tmp/glk98260.run` journal 关闭；boot_id 未知 ⇒ Refused，不写 must_reboot）；S4 成功判据独立于日志（`/proc/modules` + `su` 上下文）；S5 预检拒绝：KSU 已加载、Shizuku 未授权、身份非 shell、profile 缺失、must_reboot、release/slab 不匹配；S6 `POSIX_SPAWN_SETSID`（fallback payload `--setsid`）+ spawn 后 `/proc/<pid>` 存活确认，提示"运行期间不得强停 app/不得停 Shizuku"；S7 日志不含密钥。

## 11. 批次与写者纪律

- **B0**（本批）：分析/计划/附录 → design-critic 评审 → 用户批准。唯一写者=Lead。
- **B1**：native helper 移植+CMake+离线 native 门禁（写范围 `app/src/main/cpp/gl98260/**`、`CMakeLists.txt`）；退出条件=static ET_EXEC 构建+打包证据，不满足则停并改方案。
- **B2**：service 侧+app Flow（写范围 delta §4）；前置关闭 N-26～N-29。
- **B3**：app 侧 client/UI/身份/中性状态/registry/strings/dispatcher 接线+全量单测；`blocked_by: task-32 落地`；门禁在最终树或隔离 worktree 跑并记录树身份（禁本树写回）。
- **B4**：文档同批+门禁 pending 记录+最终报告（不 commit/push）。
- **R1a–R1d（结构适配，细则见补编 §6）**：runner 注册→exploit artifact→victim 端口→log fd→**R1d finish（攻击路径：B1 重跑+真机 pending+知情确认）**；R2 另立。
- 纪律：同一时刻单写者；Gradle 门禁串行；每批 EXIT=0 再进下批；门禁红即停，连续 2 次回退设计。

## 12. 技术债与回滚（F2 全表见附录 B §M）

TD-1 **关闭**（newProcess 作废）；TD-2 debug-only profile（越权，用户确认）；TD-3 static ET_EXEC；TD-4 数值推导；TD-5 UML 混合权威；TD-6 standards 指针；TD-7 上游 LICENSE 缺失（发布阻断）；TD-8 诊断 mode；TD-9 探针缓存；TD-10 后端触点；TD-11 install 仅 size；TD-12 残留清理；TD-13 persist-failed 窗口；TD-14～22 见附录 B §M。**第三轮 critic 总判=可进批准 gate（阻断 0）；N-16～N-20 已按 critic 最小修法关闭（附录 A §R2/附录 B）。**

**回滚**：仅当 BootSafetyStore 显示本 boot 无 `launch_pending/must_reboot` 或设备已真重启（boot_id 变）时，才可清 prefs/临时文件；armed/parked 后只提示重启（附录 A §N）。代码回滚=删新增文件+`edit` 逆改（禁 `git checkout`）。

## 13. 用户批准（2026-10-28T09:15）与强制确认

1. 批准 B1→B4 全部实现。
2. 强制确认（AGENTS §1 例外）：① **debug-only profile** 为本批唯一参数权威（越 AGENTS §4；release 不可运行；偿还 TD-2）；② 无真机，接受 **device-gate-pending**、不宣称 supported/可用。沿革见附录 B §M(TD-2)。
3. **D2 修订（09:20 用户裁定）**：执行通道改为 **UserService+JNI**（delta），其余批准项不变。
4. **结构适配补编 v2.1 批准（2026-10-28T12:4x）**：`R1a→R1c→R1e→R1b+R1d`；R2 另立；R1d 属攻击路径变更（B1 门禁重跑+真机 finish gate pending，用户已知情确认）；debug-only profile/device-gate-pending 口径沿用。

## 14. 执行进度（2026-10-28）

**B1 ✅ / B2 ✅ / B3 ✅ / B4 ✅（离线/文档；真机 pending）**：证据 `docs/analysis/98260-b{1,2,3,4}-*20261028.md`；debug/release 构建绿、R8 keep 已核。**结构适配 R1a–R1d ✅（离线，见 `98260-r1-progress-20261028.md`；真机 pending）**；**真机 G0–G14 pending**（无设备）；既有 5 红项归属在途 finish。未 commit/push。
