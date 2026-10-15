# 双 backend JNI 边界 + Kotlin 高阶步骤（实验分支 dev-nocpp · 2026-10-14）

状态：设计定稿，待执行。执行者按 §6 的批次顺序、单写入流推进；每批的判据与证据见该批末两行。
本件只定**边界与分工**：不改 route/primitive，不动 profile wire，不动 Lua 脚本面。

## §0 位置、取代与保留

| 关系 | 对象 | 说明 |
|---|---|---|
| **取代** | `docs/analysis/jni-six-targets-params-plan.md` §4/§7（三入口 + 16 字节头 + jlong 打包写请求） | 该件的参数化调查（§1–§3 与附件）保留为参考；其**边界形状作废** |
| **取代** | `core/src/jni/ghostlock_jni.cpp` 现有三入口、`core/src/jni/session_frame.hpp` 的 1 版帧提案 | 新形状见 §3；旧件在 J5 迁移完成后删除或改名 `*_legacy` 并在同批登记 |
| **补充** | `docs/analysis/access-capability-contract-plan.md`（Kotlin 契约批次） | 继续有效；本件在其上定 JNI 协议与 steps 层 |
| **不做** | route/primitive 实现、profile wire、Lua 脚本面、terminal/handoff 编排 | 见 §6 J8 |

**在办计划一致性核对（AGENTS §2）**：`docs/plan/ROADMAP.md`、`docs/plan/MASTER-PLAN.md` 为生产排期，本分支不适用（用户 2026-10-14 裁定）；`jni-six-targets-params-plan.md` ⇒ 判定**推翻其边界形状**；`access-capability-contract-plan.md` ⇒ 判定**并入/补充**；两份不并存为边界权威。

## §1 事实基线（执行前逐条核对，带物证）

| # | 事实 | 物证 |
|---|---|---|
| F1 | `core/src/jni/**` 为**未跟踪**实验件；`nativeRunOneWrite` 不可能写出任何字：`Session.race` 全零 ⇒ 在 `words == nullptr` 处具名拒绝（step=1、返回 false ⇒ JNI 返回 -4） | `core/src/jni/ghostlock_jni.cpp:34-47,103-107`；`core/src/jni/race_driver.cpp:47-52` |
| F2 | 现有入口**不读帧体**：create 只读 16 字节头 | `core/src/jni/ghostlock_jni.cpp:66-85` |
| F3 | 库名不一致：Kotlin 声明 `ghostlock_jni`，root 任务把 `build/native/ghostlock` 复制为 `libghostlock.so` | `app/src/main/kotlin/com/ghostlock/app/jni/NativeEntries.kt:28`；`build.gradle.kts`（`prepareGhostlockJniLibs`） |
| F4 | root 任务 `buildGhostlockNative` 的 `workingDir(file("src"))` 指向**不存在**的 `<repo>/src`，目标 `ghostlock` 亦无对应 Makefile | `build.gradle.kts`；`find . -maxdepth 3 -name 'Makefile*'` 只命中 `tools/avbcheck/Makefile` |
| F5 | 工作树**已删** `core/Makefile`、`core/CMakeLists.txt`、`core/fixtures/**`、`core/src/tests/**`（119 件，未暂存删除；**J0 已解决**：core/Makefile 由 core/CMakeLists.txt 取代，tests 全部回位——见 §9） | `git status --short -- core`；`git ls-tree -r --name-only HEAD core/src/tests \| wc -l` ⇒ 119 |
| F6 | 只有 `:app:compileDebugKotlin` 可用；`:app:processDebugMainManifest`/`:app:testDebugUnitTest` 不可用：`AndroidManifest.xml` 第 33–49 行**注释嵌套**，XML 在第 37 行解析失败 | 本机 `python3 -c "ET.parse('app/src/main/AndroidManifest.xml')"` ⇒ `XML FAIL: not well-formed (invalid token): line 37, column 12` |
| F7 | `profile-core` 被 `settings.gradle.kts` 排除（`kotlin-disable-20261010`）；其 `SessionSecretFrame.kt` 是 SA 帧的历史 Kotlin 实现 | `settings.gradle.kts` 注释行；`profile-core/src/main/kotlin/com/ghostlock/app/data/profile/SessionSecretFrame.kt` |
| F8 | `app/src/main/jniLibs/arm64-v8a/*.so` 为 2026-10-10 旧树产物，不得当作本树证据 | `ls -la app/src/main/jniLibs/arm64-v8a` |

> F5 是**唯一**阻塞前提：没有 native 构建入口就没有任何判据。故 J0 先落。

## §2 一条操作何时必须留 native（判据）

任一成立 ⇒ 留 native；全部不成立 ⇒ 搬 Kotlin（默认搬）。

| 判据 | 含义 | 本项目的例 |
|---|---|---|
| N1 时序 | 需要多线程/多进程在**同一个内核原语窗口**内协作，中间不能被 JNI 往返切开 | PI 窗口三角色、`FUTEX_CMP_REQUEUE_PI` 触发、`punch_consume` |
| N2 资源 | 资源（fd/socket/SA/sequence/映射/子进程）必须跨调用连续持有，且释放顺序有硬要求 | 43284 门 + SA + sequence；43499 的字/载荷页/守卫/路由引擎 |
| N3 不可达 | 目标内存或文件本进程读不到，必须借桥（crash_dump / perf / splice） | vendor 只读文件页缓存；内核地址 |
| N4 秘密 | 材料（AES/HMAC key、SPI、端口）不得进入 Java 堆或跨调用留存 | SA 会话帧 |
| N5 判据 | 结论由一段**已过真机验证的规则**给出，搬走即第二权威 | `steps::wait_for_module` 的 failure→success 顺序、模块出现又消失=失败 |

**不属于 N1–N5 的一律搬 Kotlin**：步骤选择与顺序、重试次数与间隔、地址算术、值的选择、日志文本、验证判定（除 N5）、能力门控、进度与结果归档。

## §3 JNI 边界

### 3.1 四个 extern（唯一入口面）

```
JNIEXPORT jlong     nativeSessionCreate (JNIEnv*, jclass, jbyteArray createFrame);
JNIEXPORT jbyteArray nativeSessionInvoke(JNIEnv*, jclass, jlong handle, jint op, jbyteArray requestFrame);
JNIEXPORT jint      nativeSessionShutdown(JNIEnv*, jclass, jlong handle);
JNIEXPORT void      nativeSessionDestroy (JNIEnv*, jclass, jlong handle);
```

- 类名必须仍是 `com.ghostlock.app.jni.Native`（JNI 查名规则），`NativeEntries.kt` 保留 `object Native`。
- 句柄**带 generation**，防复用（先例 `core/src/tests/handle_generation_test.cpp`（HEAD））。
- `nativeSessionInvoke` 的返回帧自带 `status`；**不靠 Java 异常传递业务失败**。
- `shutdown` 与 `destroy` 幂等；`shutdown` 之后 `invoke` 一律返回 `closed`，不触碰任何 backend 对象。

### 3.2 帧格式（GLKJ v1）

```
header (16B):  magic "GLKJ"(4) | version u16 | op u16 | status i32 | entries u32
entry:         key u16 | kind u8 (0=u64, 1=bytes) | reserved u8 | length u32 | payload
```

- 全部 little-endian；`status` 只在回复帧有意义（请求帧置 0）。
- 拒绝名：`frame-malformed`（截断/长度不符）、`frame-version`、`op-unknown:<n>`、`key-unknown:<n>`、`key-duplicate:<n>`、`key-missing:<n>`、`key-width:<n>`。
- key 目录（唯一拼写点）在 `core/src/jni/session_protocol.hpp`；由 `make -C core manifest` 导出 `build/native/jni-protocol-manifest.tsv`（op / key / status 三张表）。Kotlin 侧**读该文件对拍**，不得手抄（AGENTS §4）。

### 3.3 op 表（每个 op 一笔 native 事务）

| op | 名称 | backend | 调用者 | 输入 key（来源） | 输出 key | 具名拒绝（例） | 复用单元（不得改写） |
|---|---|---|---|---|---|---|---|
| 0 | Snapshot | both | 会话轮询/每步前 | — | phase, caps, facts, generation | — | 会话对象自身 |
| 1 | Initialize | both | `open()` | 43499: route + stamp 几何(4) + route 调优(3) + 耐心(6) + consumer 策略(2) + offset；43284: SA 秘密帧(bytes) + 路径对(carrier/dev_carrier/patch_target/hook_image/kernel_release + 4 marker + module_name) + carrier_base + kernel 三布尔 + sequence? | — | `init-bad-params` / `init-landing-failed` / `session-frame-invalid:<码>` | `KernelAccess::create`+`initialize`；`session_frame.cpp` 读取器 |
| 2 | Prepare | 43499 | w1 prelude | region/geometry/tuning 值 | `page_base`,`words`, `payload_page`,`payload_base`,`alias_delta` | `step-words-unprepared` | `steps::run_prepare`、`steps::WordsRegion`、`memory::prepare_page` |
| 3 | Discover | 43499 | prelude / 修复轮 | scan 窗口值 | `found`,`probes`,`rounds` | `discovery-*` | `steps::run_discovery` |
| 4 | SpawnVictim | 43499 | w2 prelude | — | `victim_pid`,`victim_task` | `victim-pipes` / `victim-fork` / `victim-leak` | `backend::victim::spawn_victim` |
| 5 | VictimProbe | 43499 | w2/w3 验证 | `kind`(uid/comm) | `value` | `victim-*` | `victim_report_uid` / `_comm` |
| 6 | WriteWord | 43499 | w1/w2/w3 | `target`,`value`(8B),`form`,`offset` | `applied`(8) | `write-refused:<stage>` / `write-indeterminate` | `KernelAccess43499::write` + `WordLanding::land_word` + `WindowGuard` |
| 7 | ReadWord | 43499 | 可选验证 | `address` | `value`(8B) | `unsupported`（当前 `can_read()==false`） | `KernelAccess43499::read` |
| 8 | ResolveTarget | 43284 | 每步开头 | `role`,`path` | `token` | `role-unknown` / `door-refused` / `bridge-unbound` | `pagecache_door_linux` + `crash_dump_bridge` |
| 9 | WriteBlock | 43284 | 每个 16B 块 | `token`,`offset`,`bytes`(16) | `applied`(16) | `block-refused:<state>/splice=<码>` | `PageCacheLanding::land_block` |
| 10 | ReadBlock | 43284 | 读旧块/回读 | `token`,`offset` | `bytes`(16) | `read-unsupported` / `read-failed` | `PageCacheLanding::read_block` |
| 11 | Trigger | 43284 | trigger 步 | `delay_ms` | — | `trigger-*` | `steps::trigger_orphan` |
| 12 | WaitTerminal | 43284 | wait 步 | `wait_ms`,`poll_ms`,4 marker,`module_name` | `terminal`(success/failure/inflight) | `wait-budget-exceeded` / `wait-failure-marker` / `module-gone` | `steps::wait_for_module`（N5） |
| 13 | EmbeddedImage | 43284 | patch1 步 | `role`(splice_helper / sentry_payload) | `bytes` | `image-unknown` | `embed::splicehelper_bytes` / `libcxx_blob` |
| 14 | HookWords | 43284 | hook 步 | — | `branch_word`,`displaced_word`,`site_offset`,`landing_offset`,`payload` | `plan-refused:<码>` | `hook::device_sentry_plan` + `hook_branch` + `build_sentry_payload` |

**规则**

1. op 只是「一笔 native 事务」的入口，不是步骤调度器：**Kotlin 决定调哪个 op、什么顺序、重试几次**。
2. 43499 的 route、offset、几何、调优、耐心、consumer 策略全部由 Kotlin 从 profile 取值后随帧传入；native 用既有的 `usable_for_initialize()`/`usable_for_access()`/`assembly_complete()`/`consistent()` 校验，缺项 ⇒ `init-bad-params`，**绝不填默认**。
3. `offset == 0` 合法 ⇒ 必须用 `offset_set` 独立表达（既有约定，`access_params.hpp:39-42`）。
4. `WriteWord` 的 `form` 是**闭集**（见 §4.1），native 映射到 ` (mode, leaf) ` 并在描述符里公布可用集合；未知组合 ⇒ `write-refused:form`。
5. 写效果不可确认 ⇒ 状态 `indeterminate`（**不是成功**）；Kotlin 映射 `AccessFailure.INDETERMINATE` 并冻结引擎。

### 3.4 生命周期、线程与秘密

- **单会话一锁**：native 会话对象内串行化所有 `invoke`；重入 ⇒ `busy`。Kotlin 侧同样串行（单线程 dispatcher + `Mutex`）。
- **shutdown 顺序**（AGENTS §5，不得因重构改变）：停止访问 ⇒ 同步/解除关联 ⇒ 回收；成功/失败/取消三条路径都要走到。
- **取消**：进入 native 后不可中断；返回后先登记回执再传播取消（`cancellationObserved` 语义）。
- **秘密（N4）**：SA 帧以 `bytes` entry 传入；native 在**本次调用内**解析成 `SaMaterial` 并复制；Kotlin 在 `finally` 里清零自己的缓冲，native 在所有失败分支清零临时缓冲。Kotlin 侧用 consume-once 类型（`SensitiveFrame`：`toString()` 只回 redacted，不进 `data class`，不写日志）。

## §4 Kotlin 侧（能力接口 → 高阶步骤）

### 4.1 契约增补（C1：WriteForm）

43499 的 W1 与 W3 都是「写零」，但落字形状不同（W1 保留已初始化字节、W3 走叶子臂）⇒ `(address, bytes)` **推不出**该选哪个。故增补一个**闭集**参数，并把可用集合放进描述符（native 权威）：

```
/** 闭集：native 按自己的表映射到 (mode, leaf)；未知组合具名拒绝。 */
enum class WriteForm { ClearPreserving, ClearLeaf, ValuePreserving }

interface KernelMemoryAccess : AccessCapability {
    fun read(address: KernelAddress, sizeBytes: Int): AccessResult<ByteArray>
    fun write(address: KernelAddress, bytes: ByteArray, form: WriteForm): AccessResult<WriteReceipt>
}
```

- 43499 ⇒ `read = null`（当前 `can_read()==false`）、`write = OperationLimits({8}, 8, repeatable = true)`、`writeForms = {ClearPreserving, ClearLeaf, ValuePreserving}`。
- 43284 ⇒ `write = OperationLimits({16}, 1, repeatable = true)`、`read = OperationLimits({16}, 1, repeatable = true)`、`writeForms = {}`。
- CBN：备选一＝只留 `(address, bytes)` 让 native 猜形状（否：W1/W3 无法区分，选择权藏进实现）；备选二＝Kotlin 传裸 `mode/leaf` 两个 int（否：第二权威）。翻转条件：出现第 4 种落字形状且无法命名时重做本条。

### 4.2 包与文件

```
app/src/main/kotlin/com/ghostlock/app/core/                  # 已有 7 件；本批只增 4.1 的 WriteForm 与描述符字段
app/src/main/kotlin/com/ghostlock/app/core/steps/            # 新增：高阶步骤（W1/W2/W3 与 pagecache 五步）
  StepId.kt  StepNames.kt  StepEngine.kt  StepInputs.kt  StepResult.kt
  PlanCatalog.kt            # 名称/顺序的唯一拼写点；声明顺序在此校验
  FourNinetyNineSteps.kt    # w1 / w2 / w3
  PageCacheSteps.kt         # patch1 / carrier / hook / trigger / wait
app/src/main/kotlin/com/ghostlock/app/jni/NativeEntries.kt   # 改成 §3.1 的四个 extern（保留 object Native）
app/src/main/kotlin/com/ghostlock/app/data/nativeaccess/     # 新增：JNI 适配（平台 I/O，不进 core 契约包）
  NativeProtocol.kt  JniAccessSession.kt  JniAccessSessionFactory.kt
  JniKernelMemory.kt  JniPageCache.kt  SensitiveFrame.kt  DescriptorCodec.kt
```

依赖方向固定：`data.nativeaccess → app.core`；`app.core` 不依赖 Android/JNI。

### 4.3 步骤表 — 43499（Kotlin 组装）

| 步 | 前置（缺 ⇒ 不调 native） | 动作（能力调用序列） | 验证 | 失败名 |
|---|---|---|---|---|
| `prepare`（prelude） | 会话 READY | op2 Prepare（值来自 profile） | facts 含 `page_base`/`words`/`payload_page` | `prepare-refused:<原因>` |
| `discover`（prelude） | — | op3 Discover | facts 含 `found`/`alias_delta` | `discovery-refused` |
| `spawn_victim`（prelude） | — | op4 SpawnVictim | facts 含 `victim_task`/`victim_pid` | `victim-spawn-refused` |
| `w1` | prelude 完成；`kernelMemory` 可写；`ClearPreserving ∈ writeForms` | `write(aliasBase + off(selinux_enforcing), 8×0, ClearPreserving)` 按 `w1_attempts`/`w1_settle_us` 重试 | `/sys/fs/selinux/enforce` 可读 ⇒ 比较 0；不可读 ⇒ `verification = Unavailable`（不算成功） | `w1-<native 名>` |
| `w2` | `spawn_victim` 完成；`ValuePreserving ∈ writeForms` | `write(victimTask + off(task_cred), credBytes, ValuePreserving)` 按 `w2_attempts`/`w2_settle_us` 重试 | op5 `VictimProbe(uid) == 0` | `w2-not-rooted` |
| `w3` | `w2` 判据成立 | `write(victimTask + off(thread_info_flags), 8×0, ClearLeaf)`、`write(victimTask + off(seccomp_mode), 8×0, ClearLeaf)` | op5 VictimProbe +（若描述符有读）回读 | `w3-<native 名>` |

- 目标地址 = 运行期 facts（native 发布）+ profile 偏移（Kotlin 计算）；「base + offset，任一为零即拒绝」照搬 `steps/targets.hpp`。
- 值（credential 片段）= profile 数据，Kotlin 组装；**机制**（arm/窗口/重挂）全在 native。
- 具体键名（`task_cred`、`thread_info_flags`、`seccomp_mode` 等）以 schema/profile 的现有键为准，**不得猜**；取值处只写一处。

### 4.4 步骤表 — 43284（Kotlin 组装）

| 步 | 前置 | 动作 | 验证 | 失败名 |
|---|---|---|---|---|
| `patch1` | READY；`pageCache` 可写可读 | op8 `resolve(role=patch1)`；op13 `EmbeddedImage(splice_helper)`；对每块 `write(t, i, blk)` | 每块写后 `read(t, i)` 相等 | `patch1-block-<n>` |
| `carrier` | 同上 | op8 `resolve(role=carrier)`；Kotlin 读 .ko 字节（profile 给的路径）；对每块 `write(t, carrier_base + i, blk)` | 每块回读相等 | `carrier-block-<n>` |
| `hook` | 同上 | op8 `resolve(role=hook)`；op14 `HookWords`；`blk = read(t, site_offset)`；校验其中 `displaced_word` 相符；替换为 `branch_word` ⇒ `write`；再把 `payload` 逐块写到 landing | 每块回读 + 分支字回读 | `hook-guard-mismatch` / `hook-block-<n>` |
| `trigger` | `hook` 完成 | op11 `Trigger(delay_ms)` | — | `trigger-<native>` |
| `wait` | `trigger` 完成 | op12 `WaitTerminal(...)` | native 判据（N5） | `<native 具名>` |

- 整条链**一个会话**：SA/门/sequence 由 native 持有并跨 op 延续；Kotlin 不得中途重开会话（会重置 sequence ⇒ 真机已被反重放丢包证实）。
- Kotlin 只读**自己的** payload 文件（.ko）；目标载体一律经 `resolve` 的 token 写入，不得自行 `open`。

### 4.5 引擎规则

1. 顺序 = 输入声明的顺序，经 `PlanCatalog` 校验（未知步名 ⇒ `step-unknown`，不得静默跳过）。
2. 缺前置 ⇒ 该步 `Refused("precondition:<which>")`，**不进入 native**。
3. 任一步 `Refused` ⇒ 立即停止。
4. 写效果 `INDETERMINATE` ⇒ 引擎进入 `Frozen`：除 `snapshot`/`shutdown` 外不再发出任何调用，**不得重试**。
5. 重试次数/间隔**只**来自输入值；0 ⇒ 不重试，不填默认。
6. 每步产出 `StepResult{step, outcome, reasonToken, receipts[]}`；日志一行/步（拒绝走 `warn`）。

## §4.6 UI 裁定（2026-10-14，用户逐字追加）

> 「舍弃旧 kotlin UI，测试用最小化临时 UI，以后新的 UI 将使用 materialUI 并像 kernelsu 一样可灵活切换至 miuix」

落地形状（本批已实现骨架）：

- `app/ui/kit/UiKit.kt`：套件枚举 + `UiKitStore` 接口 + `UiKitController`（读盘即真相，写即落盘）。
- `app/ui/kit/AndroidUiKitStore.kt`：SharedPreferences 实现（键与取值只在此拼一次）。
- `app/ui/kit/GhostlockKit.kt`：组件面（Actions / Action / Body）的 **两个实现**（Material3 与 Miuix），
  界面只依赖语义、不 import 具体控件 —— 与 KernelSU「每个屏幕两套实现、同一组语义」同形。
- `app/ui/theme/GhostlockTheme.kt`：按套件包主题（Material3 colorScheme / MiuixTheme）。
- `app/tempui/TempUiActivity.kt`：临时界面，顶部按钮即套件切换，选择落盘、切完立刻重绘。

两条环境事实（踩过就写下来）：miuix 0.9.4 依赖 material3 **1.5.0-alpha22**，声明更低会被冲突解析
顶上来；`monetSystemColors` 是 miuix 的 **internal** API，编译期不可用，故主题用其公开的
`light/darkColorScheme()`。

## §5 事实分工表（防第二权威）

| 事实 | 产生者 | 读取者 | 备注 |
|---|---|---|---|
| profile 值（偏移、几何、调优、路径、marker、重试数） | Kotlin（profile 装载层） | Kotlin（算地址/传帧）、native（只读帧） | native 不再解析文档；缺项 ⇒ `init-bad-params` |
| 运行期事实（`page_base`、`words`、`found`、`alias_delta`、`victim_task`、`token`） | native | Kotlin（经 `snapshot`/op 输出） | 只发布不解释 |
| 目标地址 | Kotlin（facts + profile 偏移） | native（校验 direct-map/范围） | 两侧都校验，权威在 native |
| SA/密钥/SPI/端口 | Kotlin 传入 ⇒ native 持有 | native | 不落日志、不落文档、用后清零 |
| sequence | native | native（Kotlin 只见 `snapshot` 只读值） | 跨 op 连续，禁止重置 |
| 判据（wait 终态、write 验证） | native | Kotlin | N5；搬走即第二权威 |

## §6 执行批次（单写入流，逐批验收）

### J0 构建入口（前置；不动攻击代码）

- **写范围**：新建 `core/Makefile`；改 `build.gradle.kts`。
- **动作**
  1. `core/Makefile`（**新写文件**）：以 `git show HEAD:core/Makefile`（**只读**）为蓝本，去掉本树已不存在文件对应的条目；四个目标：`host`（宿主二进制）、`check`（跑宿主测试）、`jni`（NDK 出 `build/native/libghostlock_jni.so`）、`clean`。
  2. 禁止用 `git checkout/restore` 或 `git show REV:path > path` 恢复被删件（AGENTS §6）：J0 的 Makefile 是**新写**。
  3. `build.gradle.kts`：新增 `buildGhostlockJni`（`workingDir(file("core"))` + `make jni`，NDK 环境变量与既有任务一致）与 `prepareGhostlockJniLibs`（复制为 `app/src/main/jniLibs/arm64-v8a/libghostlock_jni.so`）；把 `buildGhostlockNative` 的 `workingDir` 修成 `core`、目标名与 Makefile 对齐，或标 `legacy` 并摘出默认依赖。
- **判据**：`make -C core host` EXIT=0；`make -C core jni` EXIT=0 且 .so 存在；`nm -D --defined-only <so> | grep -c Java_com_ghostlock_app_jni_Native_` ≥ 3（既有 legacy 三入口；J5 后应含新四入口名）。
- **证据**：命令、退出码、`shasum -a 256 <so>`、`nm` 片段、`realpath` 得到的 .so 路径。

### J1 协议层（native）

- **写范围**：`core/src/jni/session_protocol.hpp/.cpp`（新）、`core/src/jni/jni_protocol_manifest.cpp`（新）、`core/src/tests/jni_protocol_test.cpp`（新）、`core/Makefile` 挂测试与 `manifest` 目标。
- **动作**：按 §3.2 实现编解码与 key 目录；`static_assert` 钉住 header 布局与 key 枚举；导出 manifest。
- **判据**：`make -C core check` EXIT=0；覆盖 round-trip、截断、坏 magic、坏版本、未知 op、未知 key、重复 key、长度不符、空请求、`bytes` 上限、**secret entry 清零**；另做**一次证伪**（改一位 ⇒ 对应断言红、退出码非 0；撤回后无残留）。
- **证据**：`check` 的 PASS/FAIL 计数（照抄）、manifest 首若干行、证伪前后的两条退出码。

### J2 会话对象与生命周期（native）

- **写范围**：`core/src/jni/session_object.hpp/.cpp`、`core/src/jni/session_api.cpp`（四个 extern）、`core/src/tests/jni_session_lifecycle_test.cpp`（新）。
- **动作**：句柄 generation；phase 状态机（`declared/initializing/ready/failed/closed`）；串行化；`shutdown` 顺序；`busy`/`closed` 具名状态。
- **判据**：宿主测试（假 backend 桩）覆盖：重复 create、close 后 `invoke` ⇒ `closed`、重复 shutdown 返回同一份回执、destroy 幂等、并发 invoke 串行化或 `busy`、未知写效果 ⇒ `indeterminate`、shutdown 顺序有日志标记；`make -C core check` EXIT=0。
- **证据**：`check` 计数 + 一份关键测试的原始输出。

### J3 43499 ops（native）

- **写范围**：`core/src/jni/ops_43499.cpp`（新）、`core/src/tests/jni_ops_43499_test.cpp`（新，假 landing 记录调用顺序与参数）。
- **动作**：实现 op 2/3/4/5/6/7；**复用**（不改写）`steps::run_prepare`/`run_discovery`、`victim::spawn_victim`、`KernelAccess43499`、`WordLanding::land_word`、`WindowGuard`；字/载荷页/守卫由**会话对象**持有（归属规则：要活过准备那一段的东西归调用方）。
- **判据**：缺 facts ⇒ 具名拒绝且未进入 route；窗口守卫在成功/失败/超时三条路径都释放；`form` → `(mode, leaf)` 映射与 `memory::write_layout` 一致；`ReadWord` 在 `can_read()==false` 时返回 `unsupported`；`make -C core check` + `make -C core jni` EXIT=0。
- **证据**：`check` 计数、.so 哈希、越界/缺项两条拒绝的原始日志。

### J3a / J3b：43499 ops 的分工（2026-10-14 实际执行时的裁定）

J3 在执行时拆成两半，理由是**可验证性**：

- **J3a（本批已完成）**：op 层本身 —— 形状闭集 `WriteForm` → `(mode, preserve)` 的映射表、
  `Facts43499` 与"三件事实齐了才允许写"的门、每个拒绝的名字、以及 landing 被调用的实参
  （地址/值/偏移/形状）。全部在宿主上可测，实测 125/125 CTest 绿，并做过一次证伪（把
  `ClearLeaf` 的 preserve 反极性 ⇒ 表单测试红）。
- **J3b（未做，需要设备）**：把 `Ops43499Seams` 接到真单元 —— `steps::run_prepare`、
  `steps::run_discovery`、`victim::spawn_victim` / `victim_report_uid`、以及 `land` 背后的
  路由引擎（`landing::MulticastRoute` + 窗口 + `KernelAccess43499`）。没有设备就无法验证，
  所以本批不写"看起来能跑"的设备代码：landing 为空时每个 op 都以 `op-not-wired:<名字>` 具名拒绝。

> 同一条规则适用于 J4：先做可测的 op 层，设备 landing 单列。

### J4 43284 ops（native）

- **写范围**：`core/src/jni/ops_43284.cpp`（新）、`core/src/tests/jni_ops_43284_test.cpp`（新）。
- **动作**：实现 op 8/9/10/11/12/13/14；复用 `PageCacheLanding`、`pagecache_door_linux`、`crash_dump_bridge`、`steps::trigger_orphan`、`steps::wait_for_module`、`embed::*`、`hook::*`。
- **判据**：SA 帧坏 ⇒ `session-frame-invalid:<码>`；未 initialize 就写 ⇒ `not-ready`；非 16B 宽度 ⇒ `bad-width`；sequence 跨 op 递增；token 跨会话/跨 generation ⇒ `stale-target`；`make -C core check` + `make -C core jni` EXIT=0。
- **证据**：同 J3。

### J5 Kotlin 桥（含 manifest 对拍与 manifest 修复）

- **写范围**：`app/src/main/kotlin/com/ghostlock/app/jni/NativeEntries.kt`（改四 extern）、`app/src/main/kotlin/com/ghostlock/app/data/nativeaccess/*.kt`（新 7 件）、`app/src/test/kotlin/com/ghostlock/app/data/nativeaccess/*Test.kt`（新）、`app/build.gradle.kts`（Copy 任务把 `build/native/jni-protocol-manifest.tsv` 送进 `app/src/test/resources/`）、`app/src/main/AndroidManifest.xml`（**修 F6**：第 33–49 行注释嵌套，保持「这些组件已禁用」原意，去掉内层 `<!-- -->`）。
- **判据**：`./gradlew :app:testDebugUnitTest --offline --console=plain` EXIT=0（含 manifest 与 Kotlin 表逐项对拍、宽度/对齐预筛、未知状态码 fail-closed、关闭后调用不触碰 native 的假桩断言）；`./gradlew :app:compileDebugKotlin --offline --console=plain` EXIT=0。
- **证据**：两条命令 + 退出码 + 测试报告路径（失败读 `build/test-results/**/*.xml` 全量）。

### J6 Kotlin 高阶步骤

- **写范围**：`app/src/main/kotlin/com/ghostlock/app/core/steps/*.kt`（新 8 件）、`app/src/test/kotlin/com/ghostlock/app/core/steps/*Test.kt`（新）、`app/src/main/kotlin/com/ghostlock/app/core/KernelMemoryAccess.kt` + `AccessDescriptor.kt`（C1 增补）。
- **判据**：`:app:testDebugUnitTest` EXIT=0，覆盖：顺序固定（w1→w2→w3 / patch1→carrier→hook→trigger→wait）；缺能力/缺 facts ⇒ 不进入 native 且具名拒绝；中途失败即停；`INDETERMINATE` 冻结引擎；重试数为 0 ⇒ 不重试；空/零值输入 ⇒ 拒绝；未知步名 ⇒ `step-unknown`。
- **证据**：同上（XML 全量计数）。

### J7 门禁与设备（判据汇总）

- **判据**：`make -C core check`、`make -C core android`（NDK 零告警）、`./gradlew markdownlint`、`:app:testDebugUnitTest` 全绿；真机（若设备在）：单 backend、冷机、固定 CPU 对、KernelSU 未加载，日志归档 `docs/analysis/device-gates/`。
- **同批**：更新 `docs/development/full-process-uml.md`（class/state/sequence），提交信息写明改了哪张图。
- **纪律**：无设备证据时，任何「写已落下/root 成功」不得进结论；真机旁路须同批登记清理计划。

### J8（可选，本计划外）真实 profile 接线与终态交接

`profile-core` 目前被排除；J6 的步骤输入经 `ProfileValues` 接口注入（测试用假实现）。接真实 GLKv3 文档、以及 terminal/handoff 的归属，各开独立批次。**翻转条件**：若 Kotlin 侧无法在合理代价内取到全部 profile 值，允许退回「文档整块过界、native 解析」的旧路——但须作为一次显式裁决记录，不得两侧各解析一半。

## §7 验证矩阵（极端输入）

| 输入/场景 | 预期 |
|---|---|
| 帧长度 0 / 短于 header / 与 entries 不符 | `frame-malformed`，不进入 op |
| 版本 ≠ 1 | `frame-version` |
| op 未在表中 | `op-unknown:<n>` |
| key 未知 / 重复 / 缺失 / 宽度不符 | `key-unknown` / `key-duplicate` / `key-missing` / `key-width` |
| 43499 写宽 7/9，43284 写宽 15/17 | `bad-width`，不进入 provider |
| 地址 0 或越 direct-map | `write-refused`（`in_direct_map` 语义照搬） |
| `offset == 0` 但未置 `offset_set` | `init-bad-params` / `bad-params`（零不是哨兵） |
| 未 initialize / 已 shutdown 后 invoke | `not-ready` / `closed`，不触碰 backend |
| token 跨会话 / 跨 generation | `stale-target` |
| 写效果不可确认 | `indeterminate`（非成功）+ 会话 `Indeterminate` |
| 并发 invoke | 串行化或 `busy`（不产生交错写） |
| secret entry 处理后 | JNI 缓冲与 native 临时缓冲均为 0（测试断言） |
| 43499 `ReadWord` | `unsupported`（描述符 `read = null`） |

## §8 禁止事项

1. JNI 层不得出现：profile 解析（超出既有 wire 文档路径）、步骤选择、重试策略、日志文本渲染、地址算术。
2. 不得把 43499 的读报成可用（`can_read()==false` ⇒ 描述符 `read = null`）。
3. Kotlin 不得绕过 token 自行打开目标载体（自身 payload .ko 与 helper 字节除外）。
4. 跨端 op/key/status 列表不得手抄，只经 manifest 对拍。
5. 不得用 git 写回命令恢复被删文件；J0 的 Makefile 是**新写**。
6. 无设备证据不得宣称写效果或 root；不得以 host 测试代替真机判据。
7. 攻击关键路径的每批只允许**一条写入流**；共享 `build/**` 的门禁串行跑。


## §10 取代：函数面（用户裁定 2026-10-14）

**裁定原文**：不要通过 token 和 bytearray Frame 交互，每个 backend 一个 jni，并通过多个正经函数传参交互。

### §10.1 取代了什么

§3 的四个 extern（create/invoke/shutdown/destroy）+ GLKJ v1 帧 + op/key/status 三张表全部作废：
边界不再有「一个 op 名 + 一帧字节」。跨端只剩两种东西 —— 具名函数，与函数自己的类型化参数/结果。

### §10.2 新的形状

| 层 | 43499 | 43284 |
|---|---|---|
| 库 | libghostlock43499.so | libghostlock43284.so |
| 函数面 | create / initialize(long[]) / snapshot(long[]) / prepare / discover / spawnVictim / victimProbe / writeWord / readWord / shutdown / destroy / refusal / 布局自描述 | create / initialize(long[]) / snapshot(long[]) / resolveTarget / writeBlock / readBlock / trigger / waitTerminal / embeddedImage / hookWords / hookPayload / shutdown / destroy / refusal / 布局自描述 |
| 参数 | 数字、布尔、字节（路径/块/镜像那几处本来就是字节） | 同左 |
| 结果 | 调用码（int）+ 调用方给的数组 | 调用码 + 数组/byte[] |
| 失败 | refusal(handle) 给名字（not-ready:prepare / op-not-wired:write_word / stale-target / inflight …） | 同左 |
| 数组布局 | native 导出字符串表，Kotlin 对拍 | 同左 |

调用码（core/src/jni/backend_api.hpp）：0 ok；负数是 not-ready / bad-parameters / unsupported / failed /
stale-handle / busy / stale-target / inflight。名字由 call_code_name 拼一次。

### §10.3 批次

- **A（本批已做）**：native 侧类型化核心（api_43499.* / api_43284.*）+ 两个 JNI 库 + 两个库的宿主测试；
  旧分帧实现仍留在树上，类型由 api_*.hpp 提供一份，不存在两份定义。
- **B（下批）**：Kotlin 侧切成两个类型化客户端（cve43499.Native43499 / cve43284.Native43284），步骤引擎改调具名函数，
  真机门禁改走新库；然后删除分帧实现（session_protocol / session_object / session_api / ops_*.cpp 的帧部分）
  与 manifest 的 op/key/status 段。

### §10.4 为什么仍然保留这些判据

1. 未接线的设备工作继续以 op-not-wired:<函数名> 具名拒绝（不许沉默、不许假装成功）。
2. 前置不成立继续以 not-ready:<缺什么> 指名（例如 not-ready:prepare / not-ready:resolve-target）。
3. 效果不确定继续有独立答案（43284 的 inflight 是 InFlight 码，不是失败也不是成功）。
4. 跨端常量（镜像摘要、钩子四个数）继续由 native 导出、两端对拍，不手抄。
## §11 J7.5：分别用两个 backend 取到 root（2026-10-15 目标）

**目标原文**：执行至 J7.5 —— 成功分别用两个 backend 获取 root，可以先内置临时逻辑，真实 profile 还需进一步设计。

### §11.1 现状：链库在，缺的是把缝接到链库的那一层

| backend | 已有（当前树） | 缺（J7.5 要写） |
|---|---|---|
| 43499 | steps/prepare_runner::run_prepare(PrepareRequest&, PrepareReport&, …)、steps/discovery_runner::run_discovery(DiscoveryRequest&, DiscoveryOutcome&, …)、steps/access_binder、landing/multicast_landing::land_word、access_multicast_waiter、race/、route/、leak/、memory/、perf_ops_linux、abi、access_factory_43499 | Ops43499Seams 的**设备实现**：prepare/discover 把 typed 请求翻成 PrepareRequest/DiscoveryRequest；spawn_victim（**当前树没有**，退役树 6f7ded87 有，按只读方式取回参考再在本树上重写）；land 走 access_binder 拿到的 landing；read 由路由能力决定（不支持时如实 op-not-wired）；finish_victim 的 retire/park/handoff |
| 43284 | landing/pagecache_landing（write_block/read_block）、landing/real_splice_io、landing/crash_dump_bridge（patch#1 的执行器）、embed/（两张图）、hook/sentry_hook（钩子计划）、ipsec/aes256、lkm/module_facts、steps/pagecache_step_runner | Ops43284Seams 的**设备实现**：resolve（门/路径 → token）、write_block/read_block（接 pagecache_landing 与序号）、module_check（接 lkm）、trigger（双 fork）、wait_terminal（marker 轮询）；以及 43284 的 **D2 头文件重写**（Init43284 与九个请求组、ticket 不透明 index+generation、Dispatched→Applied→Verified、REMOTE_BRIDGE 依赖已验证 PATCH1） |

### §11.2 临时逻辑（本阶段允许）与它的债

- 会话参数暂用**内置临时值**：新增 core/src/jni/temporary_parameters.hpp，每个值旁写明「临时：真实 profile 待设计」，文件头登记为技术债；
- 目标机 = 当前连接的 A301SO（5.15.189-android13-8-00016-g51bba4309aac-ab14546557），其画像档已在 app/src/main/assets/profile/ 里（J8 选档逻辑可复用，但本阶段不依赖它）；
- 债的偿还时机：真实 profile 设计落地后，temporary_parameters.hpp 整体删除，参数改由 profile 装载层提供（登记见 §9 执行记录）。

### §11.3 批次（每批一提交；每批都要真机门禁）

| 批次 | 内容 | 真机判据 |
|---|---|---|
| J7.5-1 | 43284 的 D2 头文件/实现落盘（api_43284.* 重写，缝签名对齐契约） | 宿主测试：ticket 不透明、三态、REMOTE_BRIDGE 依赖、块宽 16 |
| J7.5-2 | 43284 数据缝接进新 API（两张图与钩子计划；草稿在 /tmp/api_43284_device.cpp.pending） | deviceGate 两项从 op-not-wired 改为 ok + size=1440 + 摘要与 manifest 一致 |
| J7.5-3 | 43284 设备缝：resolve/write_block/read_block/module_check/trigger/wait_terminal | 真机：patch1 落页缓存，read_block 比较后 APPLIED（日志给出 offset 与比较结果） |
| J7.5-4 | 43284 链端到端：patch1 → carrier → hook → trigger → wait | 真机：id 输出 uid=0，原始日志归档 |
| J7.5-5 | 43499 设备缝：prepare/discover/spawn_victim/finish_victim/land/read（参数用内置临时值） | 真机：prepare/discover 给出 page_base/found；三次 write_word（W1/W3 清分别、W2 凭据） |
| J7.5-6 | 43499 链端到端：w1 → w2 → w3 | 真机：id 输出 uid=0，原始日志归档 |

### §11.5 更名批（用户裁定 2026-10-15：不要 landing 这种临时感的词）

名字与批次（**每批都要 132/132 与 checkAll 绿**）：

| 批次 | 范围 | 旧 → 新 |
|---|---|---|
| R1（已做，见 git log） | JNI 边界层（我引入的那部分） | Ops43499Seams / Ops43284Seams → DeviceOps43499 / DeviceOps43284；device_landings_* → device_ops_*；成员 ops_ → device_ops_；会话模板 → 工厂 set_factory（秘密不可拷贝） |
| R2 | 43499 的落地原语 | WordLanding → WordLanding；MulticastLanding → MulticastLanding；race/futex_ops_linux → race/futex_linux（LinuxFutexOps → LinuxFutex）；perf_ops* → perf_ops*；leak/snitch_ops* → leak/snitch_ops*（SnitchOps → SnitchOps）；memory/heap_ops* → memory/heap_ops*（HeapOps → HeapOps） |
| R3 | 43284 的落地原语与目录 | landing/ 目录 → landing/；PageCacheLanding → PageCacheLanding；其余 landing/ 下的文件按同规则改名 |
| R4 | 测试与文档 | FakeOps / RecordingOps → FakeOps / RecordingOps；OpsResult → OpsResult；文档里的「缝」统一改成「设备工作 / 落地原语」 |

不做的事：不改协议字符串（op-not-wired 等），不改真机日志里已有的判据文本（证据链的可追溯性优先）。

### §11.6 ESP 路线的装配点（43284 最后一段，J7.5-3 收尾）

原语都在当前树里，缺的是把它们按会话装起来：

| 环节 | 原语（已有） | 装配要点 |
|---|---|---|
| 密码学 | landing/esp_crypto.hpp：esp_cbc_iv(old_block, desired_block, key, out)、esp_icv(datagram, size, hmac_key, icv_len, out, cap) | key / hmac_key / icv_len 全部来自 Init43284Values（会话参数已经能到达设备函数） |
| I/O | landing/real_splice_io.hpp：SpliceIoOps{pipe2, splice, vmsplice, …} 与 real_splice_io() 绑定；OldPageSource(read16)、HelperWriteSource(splice16) | 设备侧用 real_splice_io()；宿主测试绑假实现（该文件头已写明这条设计） |
| 门 | landing/pagecache_doors.hpp：PageCacheDoors::open(SaMaterial)（两扇门一起开、失败不留半开）、DoorOps{open_target, close, target_is_remote} | 谁开的门谁关；remote 载体由 bridge 读，不由本进程 open（与 RemoteBridge 角色对应） |
| 落地 | landing/pagecache_landing.hpp：PageCacheLanding 的 bring_up_with(SaMaterial)、land_block(position, 16B)、read_block(position, out)、adopt_sequence(next)、can_read()、tear_down() | 每会话一个实例；ticket 指向它；序号由 adopt_sequence 推进；写成功 Dispatched，读比较一致才 Applied |
| 触发 | landing/crash_dump_bridge.hpp：CrashDumpBridge::run(offset, sink_stdout, out) | patch#1 用它跑被改过的 helper；trigger 的双 fork 让 init 收尸后由它执行 |

装法（一批一类事）：

1. **J7.5-3a**：会话里建 PageCacheLanding（用 resolve 那扇门的 fd）→ write_block/read_block 接 land_block/read_block 与 adopt_sequence；判据：宿主测试（假 I/O）+ 真机对自己的 carrier 文件写一块并读回，Dispatched → Applied。
2. **J7.5-3b**：接 CrashDumpBridge 与双 fork 的 trigger；判据：真机上 trigger 给具名答案（成功或具名失败，不许沉默）。
3. **J7.5-4**：patch1 → carrier → hook → trigger → wait 端到端，id 报 uid=0，原始日志归档 device-gates/。

现在（2026-10-15）deviceGate 的 26 项里，43284 的 write_block 仍如实回答 block-refused —— 在 3a 落地前这一点不会变。

**3a 的确切形状（2026-10-15 核实）**：

- PageCacheLanding 的构造是 PageCacheLanding(PageCacheWindow &window)：窗口是借来的，必须比落地对象活得久；
- PageCacheDoors(DoorOps &ops, int handed_in_fd = -1)：**调用方递进来的 fd 只被使用、不被关闭**（"谁开的门谁关"）
  —— 正是接 resolve 那扇门的地方；
- DoorOps{open_target, connect_esp(SaMaterial), close_fd, target_is_remote} 需要一份设备侧实现（ESP socket 用 sa 里的端口与密钥）；
- 窗口的具体实现见 landing/pagecache_write.hpp（把门与 SpliceIoOps 装起来）；bring_up_with 会在 sa.set 为假时拒绝 ——
  与 sa_material_of 的"零密钥不是凭据"是同一条规则。

因此 3a 的写码顺序是：①设备侧 DoorOps 实现（open_target=用递进来的 fd、connect_esp=按 sa 建 UDP socket）；
②窗口 + 落地对象按会话建立（lazy，随 ticket）；③write_block/read_block 走 land_block/read_block + adopt_sequence。


### §11.7 重要勘察结果：43284 的设备侧整链驱动器已经在树里（2026-10-15）

core/src/tools/pagecache_load_lkm.cpp 不是零散工具，它是**分阶段的整链驱动器**，每一阶段都有名字与参数契约：

| 阶段 | 行 | 作用 |
|---|---|---|
| phase_credentials(SaMaterial &) | 255 | 从帧里取凭据（SPI/端口/ICV/两把密钥） |
| phase_module(release, kernel_vermagic, size, facts) | 283 | 读模块镜像并取事实（vermagic 等） |
| phase_patch1(Access &, AccessParams &) | 328 | patch#1：把载体页改成 helper |
| phase_hook(Access &, AccessParams &, SentryPlan &) | 359 | 钩子计划与落地 |
| phase_write(Access &, AccessParams &, module_size) | 380 | 把 .ko 写进载体（与 script 侧 carrier 步骤共用实现） |
| main() | 445 | --frame-stdin 读帧；credentials → module → patch1 → write → hook → trigger 串起来 |

参考装配（两处一致）：PageCacheDoorLinux door(carrier, len) → RealSpliceIo io(real_splice_io(), target_fd) →
PageCacheWrite window(door, io) → PageCacheLanding landing(window) → landing.bring_up_with(sa)。

载体与触发（该文件头部记的实测结论）：载体 /vendor/lib64/libbinderdebug.so（或一次性载体 /data/local/tmp/ghostlock-carrier）；
触发 = **双 fork + exec_path = nullptr**，让 init 收尸时由 sentry 开火，shellcode 自己 exec /vendor/bin/insmod <carrier>。

**因此 J7.5-4 的路线是「让边界驱动这条既有链」，不是重写链**：

1. 把五个阶段按会话装配进设备函数（resolve 给载体路径、write_block/read_block 给页缓存写读、trigger 给双 fork、wait 给 marker）；
2. 或者（更省事、但需确认）让应用进程直接调用这个驱动器的阶段函数 —— 前提是链必须在**应用进程上下文**里跑（zygote 派生的应用身份正是漏洞的入口），所以「用 adb 跑工具」不等于「应用里跑链」，这条要先确认再选；
3. 两个 backend 的端到端判据都留 id 报 uid=0 的原始日志归档。

### §11.8 可复用的步骤 API（2026-10-15 勘察，3a/3b 直接照此接线）

steps/pagecache_steps.hpp 已经把链上的动作做成了函数，设备函数只需薄薄一层映射：

| 设备函数 | 复用的函数（landing/ 与 steps/ 里的现成实现） | 说明 |
|---|---|---|
| trigger | const char *trigger_orphan(std::uint32_t delay_ms) | 双 fork 触发；返回值就是**具名失败原因**（成功返回 nullptr）—— 与 Trigger43284.delayMs 一一对应，几乎零适配 |
| wait_terminal | const char *wait_for_module(const WaitRequest &) | WaitRequest 带 exists/sleep_ms 两个函数指针（宿主可注入假实现），返回具名结果；三态（success/failure/inflight）按返回值映射 |
| write_block（钩子/镜像两类写） | bool write_hook(HookRequest &, const char **refusal)、bool write_image_verified(WriteRequest &, const char **refusal) | 写成功即 Dispatched；read_block 回来比较一致才 Applied |
| （统一入口） | StepOutcome run_pagecache_step(PagecacheStep, StepContext &, ...) | 若要走「同一实现、两种调用」（script 侧与设备侧），用这个 |

装配仍按 §11.7 的参考：PageCacheDoorLinux → RealSpliceIo(real_splice_io(), target_fd) → PageCacheWrite → PageCacheLanding → bring_up_with(sa)。
驱动器的输入全部来自库头文件（backend/cve_2026_43284/* 与 contract/kernel_access.hpp、session_frame.hpp），**应用侧设备函数可以复用同一批**。

接线顺序（下一轮直接执行）：3a = 装配 + write_block/read_block；3b = trigger 走 trigger_orphan、wait_terminal 走 wait_for_module（两者都是小改动）。

### §11.9 wait_terminal 接 wait_for_module：字段一一对应，映射待定（2026-10-15 勘察）

WaitRequest 的字段与 Wait43284 的字段是**一一对应**的：success_marker / failure_marker /
in_flight_marker / module_name / budget_ms / poll_ms，外加两个可注入的钩子 exists（设备传 stat，测试传集合）
与 sleep_ms（设备传 usleep，测试传计数器）。

wait_for_module 的契约（该头文件自己写明）：**先看 failure**（陈旧的成功标记不得掩盖新的失败）→ 再看 success；
模块被观察到已加载后又消失且没有标记，算失败。@return 成功时 nullptr，否则具名原因。

**未定的一点**：哪个具名原因对应 inflight、哪个对应 failure。想用一次 grep 猜没成（没抓到字面量，返回的可能是拼接/传递的字符串），
所以**没有**替换当前那版已经被真机验证过的 marker 轮询 —— 不拿可用行为去赌一次盲接。下一轮：通读
wait_for_module 的实现（steps/pagecache_steps.cpp），把具名原因与三态对应关系写清，再换实现并跑真机那条 inflight 判据。

### §11.10 用户裁定（2026-10-15）：临时逻辑只能放在 Kotlin；C++ 不新增可以放 Kotlin 的东西

**规则**：native 侧只留**设备工作**（系统调用、文件/套接字、页缓存写读、fork/exec、与既有原语库的粘合）；
任何**决定、策略、默认值、哨兵参数、字段映射、重试/预算选择、命名与格式化**都属于逻辑，一律放 Kotlin，
由应用组装成 DTO 传进来。C++ 侧不得出现「临时这样写」「先hardcode」之类的东西 —— 这类一律在 Kotlin。

**对现有代码的审计（本轮自查）**：

| 我已加的 native 代码 | 判定 | 理由 |
|---|---|---|
| device_resolve（O_RDWR 开门 + 票据） | 保留 | 设备工作（open）；票据编号是契约（D1 表里定的角色编号），不是策略 |
| device_module_check（读镜像事实 + 调既有比对规则） | 保留 | 设备工作 + 复用既有 lkm 原语；比对规则不在这里定义 |
| device_wait_terminal（marker 轮询） | 保留（待换实现） | 轮询与 stat/usleep 是设备工作；marker 文本、预算、轮询间隔全部来自 Kotlin 的 Wait43284 |
| device_trigger（调 trigger_orphan） | 保留 | 纯粘合，delayMs 来自 Kotlin |
| sa_material_of（DTO → SaMaterial） | 保留 | 边界映射；「零密钥不是凭据」那条规则本来就在 access_params.hpp（既有约定），这里只搬运 |
| device_embedded_image / device_hook_plan | 保留 | 只把既有 embed/sentry 库的字节与计划交出去，不产生决策 |

结论：**当前 native 侧没有需要搬到 Kotlin 的逻辑**；本轮之前我在 C++ 里放哨兵参数的做法已改（哨兵在 TypedBackendProbe 里组装）。
后续新增一律先问一句：这是设备工作还是逻辑？是逻辑就写 Kotlin。

**归属**：这条裁定应当并入 AGENTS §4 的代码约定；但 AGENTS.md 现在有用户未提交的改动，本文件暂不作改动，等用户那批改动落定后再由本人（或用户）合并。

### §11.11 write_block 的可套用片段（2026-10-15 定稿，下一轮直接照抄）

读到确切的构造签名（上面 §11.7/§11.8 的链条据此补全）：

- PageCacheWrite(DoorOps &doors, SpliceOps &splice, int handed_in_fd = -1) noexcept
- PageCacheDoors(DoorOps &ops, int handed_in_fd = -1) noexcept : ops_(ops), handed_in_(handed_in_fd) {}
- RealSpliceIo(SpliceIoOps io, int file_fd, int pipe_flags = 0, OldPageSource old_page = {}, HelperWriteSource helper = {});
- PageCacheLanding(PageCacheWindow &window);  // 借窗口

按会话的生命周期（RealSpliceIo/PageCacheWrite 都不可拷贝，必须挂堆上）：

    struct Session {
        PageCacheDoorLinux door;          // 载体路径来自 Kotlin 的 resolve 请求
        PageCacheDoors doors;
        RealSpliceIo io;
        PageCacheWrite window;
        PageCacheLanding landing;
    };
    std::unique_ptr<Session> g_session;   // 与门表同生命周期；会话结束/替换时销毁

    // resolve：开载体（Kotlin 给路径）→ 建 door → doors → io → window → landing → bring_up_with(sa_material_of(params))
    // write_block：landing.land_block(position, block) 成功 ⇒ Dispatched；随后 adopt_sequence(next)
    // read_block（已接线）：比较一致才 Applied —— 写完读回相等，状态机才会往前一步

Kotlin 侧负责的部分（不在 native 猜）：载体路径、偏移、块内容、序号策略、预算；native 只做开门/拼接/落地。

### §11.12 J7.5-4 可以在应用进程里做：五阶段只是库函数的薄包装（2026-10-15 定论）

读到的事实：

| 驱动器里的阶段 | 它其实调用的库函数 |
|---|---|
| phase_patch1 | steps::write_image_verified(WriteRequest{access, params, image = splicehelper_bytes(), offset = 0, width = kWriteWidth}) |
| phase_write（模块进载体） | steps::write_image_verified(WriteRequest{access, params, image = 模块字节, offset = kCarrierBase, width}) |
| phase_hook | steps::write_hook(HookRequest{access, params, plan, width}) |
| trigger | steps::trigger_orphan(delay)（已接线） |
| wait | steps::wait_for_module(WaitRequest{exists, sleep_ms, …})（已接线） |

而 Access 是**库类**（backend/cve_2026_43284/kernel_access.hpp）：explicit Access(landing::PageCacheLanding *landing)，
只是借用落地对象 —— 正是 3a 里应用进程已经建好的那一个（g_session 里的 landing，真机已验证 up 且能 land_block）。

**结论**：J7.5-4 不需要「让 adb 跑工具」，也不需要把驱动器搬进 native 新逻辑；应用进程里按同样的五步驱动即可，
而每一步的参数（载体路径、偏移、宽度、镜像角色、钩子符号与 guard 策略、delay、预算）全部由 Kotlin 组装 DTO 传入 ——
与「临时逻辑只放 Kotlin」的裁定一致。

唯一需要在 native 侧新增的只有**粘合**：把落地对象与 Kotlin 给的参数装成 WriteRequest/HookRequest/AccessParams 并调用上面三个库函数
（这不是决策，是设备侧的装配）。

下一步顺序：① 先只跑 patch1（write_image_verified，offset 0，splicehelper 镜像）+ read_block 验证；
② 载体（模块进 /vendor/lib64/libbinderdebug.so 或一次性载体）；③ 钩子；④ trigger；⑤ wait → id 报 uid=0。每步单独真机判据与归档。

### §11.13 AccessParams 的字段面（J7.5-4 第 1 步的最后一块输入，2026-10-15）

class AccessParams final : public contract::KernelAccess::AccessMiscParameters（backend/cve_2026_43284/access_params.hpp:65）：

| 字段 | 含义 | 谁来给 |
|---|---|---|
| offset / offset_set | 写进载体的位置；**0 是合法位置，所以另设 offset_set** | Kotlin（DTO 里成对传） |
| carrier（PathView） | patch#2 写模块进去的那个载体 | Kotlin（路径） |
| carrier_base | 写入时当作载体起点的位置（模块在 0，一次性载体用过别的基点） | Kotlin |
| dev_carrier（PathView） | 进程自己打得开的一次性载体（退役树里藏在 --allow-dev-target 后面的那种放松） | Kotlin |
| patch_target（PathView） | patch#1 的目标 | Kotlin（resolve 请求里的 path 已是它） |
| …（其余：通道/窗口/校验） | usable_for_initialize() / 带 offset_set 的那一组 | 由上面几项组合 |

PathView 是**借用式** {const char *bytes; std::size_t length;}（不是 NUL 结尾）—— 与 DTO 里的字节数组对应，
所以粘合时必须保证这些字节活到调用结束（放在调用点的局部 std::string 里即可）。

**第 1 步（patch1）由此完全可写**：

    Access access(g_session->landing);                        // 专用库类，借用 3a 的落地对象
    AccessParams ap{};                                        // 字段全部来自 Kotlin 的 DTO
    steps::WriteRequest wr{};
    wr.access = &access; wr.params = &ap;
    wr.image  = std::span<const std::byte>(splicehelper_bytes(), splicehelper_padded_size());
    wr.offset = 0U; wr.width = kWriteWidth;
    const char *refusal = nullptr;
    steps::write_image_verified(wr, &refusal);                // 成功后才由 read_block 回读比较

判据：真机上 patch1 成功后，read_block 读同一偏移应能读到 helper 的十六进制头部（并据此推到 Applied），
原始日志归档 device-gates/。

### §11.14 诊断：写返回真但不落盘 = 拼接面没有 helper；第二块被拒 = 载体只有 16 字节（2026-10-15）

读 PageCacheWrite::write_block（landing/pagecache_write.hpp:124-170）得到的确定结论：

1. **前置**：doors_.socket_fd() >= 0 且 splice_.can_serve_page()；否则 WriteOutcome::NotOpen 并返回 false。
2. **旧块必须读到**：splice_.read_at(doors_.file_fd(), old_block, 16, position) 必须整读成功（NoOldBlock 否则 false）——
   IV 由旧块与目标块共同决定（compute_iv），没有旧块就没有报文可发。
3. **报文的密文槽放的是页自己的字节**，由 splice_one 在发送时替换进去；代码注释把失效模式写明了：
   「Putting the desired block in this slot instead would authenticate bytes nobody sends, and the kernel, which verifies the check before it decrypts, would drop every datagram: **found on the device, where the target page never changed**.」

**所以我观察到的两件事都有精确解释（不是玄学）**：

| 观察 | 原因 |
|---|---|
| 第一块 land_block 返回真，但回读内容不变 | 我的 RealSpliceIo 构造时**没有给 helper**（HelperWriteSource 默认空）：没有页内字节的替换，内核校验通过后解出来的不是目标块 ⇒ 目标页从不改变（正是注释里记的那个失效模式） |
| 第二块（offset 16）被拒 | 我的探针载体文件**只有 16 字节**：位置 16 的旧块读取落到 EOF ⇒ NoOldBlock ⇒ false |

**修法（下一步）**：

1. 载体文件要足够长（按 patch#1 要写的镜像长度；探针里先写成 helper 镜像大小）；
2. RealSpliceIo 必须带 helper（HelperWriteSource.splice16，来自 patch#1 落地后的 helper）与 OldPageSource ——
   即「先有 helper（patch#1）→ 才能用 ESP 路线把后续的东西写进去」这个顺序本身就是链的设计；
3. 因此 J7.5-4 的正解是：patch#1 走**另一条**入口（CrashDumpBridge 的 run(offset, sink_stdout, out)：跑被改过的 helper，其输出就是页），
   而不是靠同一个窗口连写——这与我先前「用 write_block 分块写 helper」的临时思路不同，后者注定不落盘。
### §11.4 纪律（不因为这是临时逻辑而放松）

1. 每个缝的失败一律具名（§10.4 的四条继续适用）；不许沉默、不许把未验证当成功；
2. 攻击关键路径改动按 AGENTS §8：真机门禁与 device-gates/ 归档，缺一不可；
3. 每条链**各自**过真机（分别取到 root），不合并成一次；
4. 改核心攻击代码时按 AGENTS §5 做所有权与 UAF 复查，逐条核对资源准备/存活/回收。

## §9 执行记录（执行者逐批追加；不得回填未发生的通过）

| 批次 | 命令 | 退出码 | 产物/证据 | 日期 |
|---|---|---|---|---|
| J0 | `make -C core check`（迁移前基线）→ 后改 `./gradlew nativeTestHost` | 0 | 109 PASS → 121/121 CTest | 2026-10-14 |
| J1 | `ctest -R jni_protocol` | 0 | GLKJ v1 编解码 + 具名拒绝；证伪尾随字节检查 ⇒ 红 | 2026-10-14 |
| J2 | `ctest -R jni_session` | 0 | 会话状态机 + 句柄代际 + 四个 JNI 入口；证伪 shutdown 幂等 ⇒ 红 | 2026-10-14 |
| J3a | `ctest -R jni_ops_43499` | 0 | 形状表/事实门/六 op 具名拒绝；证伪 ClearLeaf 极性 ⇒ 红 | 2026-10-14 |
| J4a | `ctest -R jni_ops_43284` | 0 | 16B 整块/token/三态终态；证伪宽度检查 ⇒ 红 | 2026-10-14 |
| J4b（第一片） | `adb logcat -s GhostlockGate:I` | 0 | 两端 sha256/词偏移逐位一致（设备门禁第二次） | 2026-10-14 |
| J5 | `./gradlew checkAll` + 真机门禁 | 0 | Kotlin 边界 + manifest 对拍；设备门禁第一次 | 2026-10-14 |
| J6 | `./gradlew :app:testDebugUnitTest` + 真机门禁 | 0 | 步骤引擎六例单测 + 真机两条具名拒绝 | 2026-10-14 |
| J7 | — | — | 待办：四门禁汇总 + UML 同批更新 | |
| J8 | — | — | 待办：真实 profile 装载与终态交接 | |
