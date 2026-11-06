# R5：43499-Glued 产品 carrier 纵向计划

> 状态：**已终止并被 2026-11-05 的“所有真攻击 backend 先断开”裁定取代。R5-S2/S3 取消；本文余下 carrier 设计只保留沿革，不再提供实施 authority。**
> 日期：2026-11-05
> 父 authority：[backend-root-execution-unification-plan.md](backend-root-execution-unification-plan.md) 的 R5。
> exact scope：[43499-glued-product-carrier-ledger.tsv](43499-glued-product-carrier-ledger.tsv)。
> 用户沿革：active goal 已明确“持续推进，我确认你的任何计划”；2026-11-05 又明确“不要验证，继续”。这构成计划认可与本轮不运行门禁的指令，但**不自动等于 AGENTS §3 语义替换直接改码例外**；该唯一剩余流程授权见 §6.1。任何未执行验证都不会写成 PASS，目标设备门前 backend 不得标成 Ready/supported/selectable。
> 当前协调：2026-11-05 automatic-export 纠正使父计划 R4A 成为当前 single-writer 文档批；这不撤销 R5 的顺序门强制沿革，但 R5 本来就仍停在 design/production gate，未与 R4A 并行写 production source。
> **终止沿革**：2026-11-05 用户随后裁定先断开所有真攻击 backend，并要求 R5 不再改造旧 backend，只做断开与必要删除。该新裁定覆盖此前的 R5 production 授权；43499-Glued、43284-Glued、98260 均不在 Kotlin registry 或 APK native target graph。R5-S1 已存在的 neutral common source 不构成 backend 接线、Ready、supported 或产品成功证据。

## 1. 活跃计划关系与强制沿革

1. 本文件不是第二套 root-execution 总计划，只具体化父计划唯一的 R5 placeholder。
2. 父计划 R0-R3 与 R4 非导出部分的 source implementation 已存在；automatic export 已由用户纠正并重新打开为 R4A，production source 尚未完成；相关 fault matrix、compile、AVD、Shizuku 与 device evidence 仍为 `NOT_RUN`。
3. 用户本轮知情强制越过父计划“先完成 R4A 并偿还 R2-R4A evidence debt 再开始 R5”的顺序门，代价是：
   - current tree 的 R4A source 尚未完成，R2-R4A 运行时正确性仍未确认；
   - R5 source 完成也只能标 `source complete / gate NOT_RUN`；
   - 第一次 release、启用 43499，或宣称 supported 前，必须偿还父计划及本计划全部门禁。
4. 技术债 owner=`root-execution/R2-R5`；偿还时机=`首个 PRODUCT_ROOT backend 变成 selectable 之前`。
5. 本计划不取代 profile-v3/extractor/KMP 设计。current tree 缺失 canonical profile-v3 runtime provider 是真实阻断，见 §5；R5 不借机恢复旧 HOCON connector、GLK1/v2 authority 或 compile-time offset 副本。

## 2. 目标、成功定义与非目标

### 2.1 本批目标

把保留的 43499-Glued PIE executable 接入 current neutral execution spine，并建立唯一链路：

```text
BackendSpec/BackendPlugin
  -> RunHost stage/publish
  -> Cve43499GluedFlow
  -> BackendProcessCarrier
  -> lib43499-Glued.so (ET_EXEC)
  -> 43499 root primitive
  -> backend-private SELinux root-context preparation
  -> exact host-published RootScriptArtifact
  -> Payload -> Finish -> GLR1
  -> backend-private enforcing restoration
  -> typed backend facts + GLR1 facts
  -> RunHostSnapshot / RunReport
```

本批 source success 必须同时满足：

1. native 不再生成、选择或解释 KernelSU/Payload/Finish 产品脚本；
2. rooted worker 只执行 host 发布且 identity 已固定的唯一 root script；
3. 43499 的 policy repair、rooted child、PI/waiter/race、worker、park 与 enforcing restore 仍由 concrete backend 拥有；
4. raw uid0、process exit 0、script dispatch 与日志都不单独等于产品成功；
5. Payload/Finish 结果只来自同一 pre-created GLR1 object；
6. backend 在 profile-v3 runtime provider和目标设备门完成前保持 `DISABLED + NotReady`。

### 2.2 本批非目标

- 不把 43499-Glued 改成 JNA DSO；它继续是独立 ET_EXEC carrier。
- 不拆 W1/W2/W3、PI race、waiter、route、heap、cred、victim transaction。
- 不重建 original 43499 Kotlin/JNI/public-victim closure。
- 不实现 43284-Glued、98260 或 shared JNA operation。
- 不修改 `LegacyProfileConverter.kt`，不建立 profile 兼容桥。
- 不把 backend-specific policy repair塞进 Payload/Finish，也不生成第二份落盘 wrapper script。
- 不在本轮运行 Gradle、CTest、AVD、Shizuku 或真机门。

## 3. Current-tree 证据与缺口

### 3.1 已存在的正确边界

- `RootScriptRenderer` 已是 Payload→Finish 唯一 renderer，并生成 GLR1。
- `PublishedArtifactIdentity` 已能对同一 opened object核 device/inode/mode/size/SHA-256。
- `RootScriptResultCodec` 已严格拒绝 partial、malformed、identity replacement 与顺序漂移。
- `BackendProcessCarrier` 已是 context-free process launcher，不从 stdout 推断产品成功。
- `BackendRunFacts`、六层 `RunReport`、typed RunHost wire 与 App/Shizuku clients 已存在。
- `lib43499-Glued.so` 实际是 AArch64 PIE executable；R5 应继续 process carrier，不改成 `SHARED_LIBRARY`。

### 3.2 当前第一断点

- 两个 RunHost 遇到 `PRODUCT_ROOT` 都直接返回 `NOT_READY`，没有 publish script、launch carrier 或 decode GLR1。
- `BackendFlowInput` 只有 profile identity，没有 host-local staged profile/carrier path与 identity。
- `RunInputFreezer` 只发布最小 JSON fixture；native 43499 当前只读旧 binary profile。
- `BackendRegistry` 没有 production root plugin。
- native setup 调用 `attack::write_root_script()`；该函数同时生成 KSU product逻辑和 43499 policy repair，破坏 owner边界。
- native frontend 即使 `G` 写失败、child不可用或 KSU probe失败仍可返回 Done；exit 0无法区分 primitive、dispatch 与 cleanup。
- parked rooted parent继承 stdout/stderr，会让 Kotlin collector在 carrier main退出后仍等待 EOF。

### 3.3 不能直接删除的旧脚本部分

旧 generator 中的以下内容不是 Payload：

1. W1 后导出并修复 SELinux policy；
2. 验证 `load_policy` 确实改变 status sequence；
3. policy repair失败时保持 fail-closed事实；
4. script完成后恢复 enforcing；
5. best-effort刷新 `/proc/iomem` cache。

这些动作属于 43499 backend-private root-context preparation/cleanup。KSU binary发现、KMI、late-load、module acknowledgement 与 safe mode则属于 common Payload，必须从 native 删除。

## 4. 决策

### D1. Backend declaration 与暴露状态

首批固定：

| 字段 | 值 | 原因 |
|---|---|---|
| key | `43499-Glued` | current packaged identity |
| capability | `PRODUCT_ROOT` | 目标是执行用户 Payload，不是诊断 |
| execution domain | `APP` | 先只声明一条尚待目标设备证明的域；不预告 Shizuku 支持 |
| abort policy | `BEFORE_START_ONLY` | physical transaction开始后不得 kill |
| selection policy | `DISABLED` | canonical profile与device gate均未关闭 |
| availability | `NotReady(profile-v3-runtime-provider-missing, device-gate-not-run)` | readiness 不伪装 source presence为可运行 |

`BackendSpec` 仍不持有 ELF 名、绝对路径、Flow、Context 或 mutable state。internal `BackendPlugin` 增加 neutral `carrierArtifact: BackendCarrierArtifact? = null`，只声明 logical id、packaged filename 与 kind；该constructor default保证既有DebugProbe/Lua plugin无需假artifact，43499必须给非null declaration。

### D2. Source object lease 与 host-local artifact catalog

`RunInputArtifact.sourceToken` 继续是中立 opaque token，但不再保存 pathname。`RunInputFreezer.freeze()` 返回一个 application-side `FrozenRunInputLease`：其中 `FrozenRunInput` 只持 token、size、digest 等中立事实，lease 独占 `token -> master ParcelFileDescriptor` 与需要删除的 app-private source file。

`BackendFlowInput`新增字段冻结为`artifacts: Map<String, PublishedFlowArtifact> = emptyMap()`与`persistentStateDirectory: String? = null`；默认值只让不消费artifact/state的既有Flow保持合法。43499 Flow必须同时取得profile、carrier两个固定logical id和nonnull state directory，否则fail-closed，不能把空map/default解释成可运行。

source resolver 对每个 profile、packaged carrier 与 root-manager executable 固定执行：

1. 以 `O_RDONLY|O_CLOEXEC|O_NOFOLLOW` **只打开一次**；
2. 从该 master fd 做 `fstat`，只接受 ordinary file；
3. 通过同一 open file description 的 duplicate fd读取全部 bytes并算 SHA-256，要求读数等于 `st_size`；
4. 把 shared offset复位到 0，生成随机 opaque token并转移给 lease；
5. `toWireRequest()` 只调用 `lease.duplicate(token)`，禁止 `File(sourceToken)` 或任意 pathname reopen；
6. 全部 request duplicates构造成功后立即 close master descriptors；失败路径关闭已建 duplicates与全部 masters；仅 app-private profile source由 lease在 masters关闭后删除，packaged files永不删除。

因此 pathname即使在 freeze后被替换，request仍引用原 opened inode。`AppContainer` 是组合 owner：它注入基于 immutable plugin lookup、PackageManager 与 `PayloadRootManagers` 的 resolver；`BackendRunSessionAdapter` 不硬编码 registry或文件位置。

RunHost staging完成后发布只读 `Map<String, PublishedFlowArtifact>`：

```kotlin
data class PublishedFlowArtifact(
    val logicalId: String,
    val kind: RunInputArtifactKind,
    val path: String,
    val identity: PublishedArtifactIdentity,
)
```

- RunHost 是 staged path 与 identity 的唯一 owner；Flow只借用。
- host忽略 caller path、mode、uid、label，按 kind设 mode：`EXECUTABLE=0500`，其它输入=`0400`。
- 43499 Flow 只按固定 logical id取 profile与 carrier；不扫描目录、不接收用户自由 path。
- root script/result仍由 `RootScriptArtifact` 单独承载。

### D3. Packaged carrier 与 manager executable staging

`BackendCarrierArtifact`只声明 logical id、packaged filename和`EXECUTABLE` kind。`PackagedBackendArtifactResolver`用 plugin declaration在 App composition边界解析 `applicationInfo.nativeLibraryDir/lib43499-Glued.so`，按D2打开master fd并交给lease；RunHost从 request duplicate复制到run workspace、核size/digest、fsync、chmod `0500`、capture staged identity，Flow只执行该staged copy。

ROOT_MANAGER同样必须引用`kind=EXECUTABLE` artifact：`RootManagerArtifactResolver`按 frozen `PayloadRootManagers` 的package优先序查询已安装ApplicationInfo，只解析该package的`nativeLibraryDir/<binary>`，随后按D2打开同一object。不存在、link、非ordinary file、digest期间变化、`launchOnly=true`或empty binary均在freeze边界fail-closed。R5不让root script执行`find /data/app`、fallback `/data/adb`或`/data/local/tmp`。

R5不让Flow直接执行nativeLibraryDir，不让Shizuku猜App linker namespace，也不增加arbitrary lookup。execution domain首批固定APP；Shizuku host对43499按domain gate拒绝。

### D4. Product request到唯一 normalized root-script plan

新增shared `RunHostProductPipeline`，由App/Shizuku host共同调用，固定顺序：

```text
validate request
-> duplicate/own PFD
-> stage and close inputs
-> map the normalized request to one RootScriptExecutionPlan
-> publish exact root script + pre-created result object
-> create fresh Flow
-> run backend
-> verify/decode GLR1 only when dispatch fact permits
-> compose six-layer terminal
-> retain/delete workspace by terminal policy
```

`RootScriptExecutionPlan` 是renderer、GLR1 decoder与six-layer composer共享的唯一冻结输入，逐项携带 wire 已验证的 kind、required/severity/alwaysRun、staged path、**actual argv**与acceptance；renderer不再读取manager catalog、package目录或用户document。Decoder直接消费同一个plan，不复制第二份expectation DTO，也不得把`DETACHED_PROCESS`、`TAKEOVER_ACK`或argv降级/拼回domain document。Preview adapter只能先把domain document转换成同一plan，不能维护第二份renderer。

ROOT_MANAGER严格沿用父wire：`artifactId`必须指向host-staged executable，`args`只承载实际argv。App freeze边界从manager catalog得到固定base args，从`uname.release`一次性解析KMI，再冻结为 `late-load --allow-shell --kmi <androidN-X.Y>`，safe mode开启时追加唯一`--safe-mode`；无法解析KMI即fail-closed。`PayloadScriptRenderer`只执行`<staged executable> + exact args`，删除所有目录搜索与fallback。R5不修改AIDL或`RunHostRequestValidator`。

### D5. 43499唯一argv grammar、profile与exact-object script exec

production invocation必须恰为以下required option各一次，顺序任意；所有数值是无`+`、无前导空白、范围内的十进制：

```text
lib43499-Glued.so
  --load-prebuilt-profile <absolute-staged-profile-path>
  --profile-dev <u64> --profile-ino <u64> --profile-size <u64> --profile-mode <u32>
  --root-script <absolute-published-script-path>
  --root-script-dev <u64> --root-script-ino <u64> --root-script-size <u64> --root-script-mode <u32>
```

unknown、duplicate、missing-value、empty、relative、oversize或overflow均在attack前返回exit 20。旧参数disposition固定为：main删除`--ghostlock-app-call`生产入口，既有stdin GLK1/frame helper因`profile/**`冻结暂时保留为无caller代码并由profile-v3 closure最终处置；删除`--enable-status-record`；删除`--force-attack`且永不绕过already-rooted guard；删除`--dump-kernel-log`。`support::run_state`仍可保留为默认disabled的内部attack-stage instrumentation，不再由production argv启用，也不拥有本链路stdin。

native在attack前分别open profile与script：`O_RDONLY|O_CLOEXEC|O_NOFOLLOW`，`fstat`逐项核device/inode/size/full mode。profile decoder只从`/proc/self/fd/<profile-fd>`读该pinned object，decode后立即close；script fd写入`ExploitSession`并保持到worker handoff。Flow在spawn紧前各调用一次`identity.matches()`；SHA-256只在publish/stage边界按既有identity规则计算，不轮询。

最终script child不得再`execl(sh, script_path)`：worker在fork紧前对已核验script fd再次`fstat`并与expected identity逐项相等；child只把该fd设为唯一CLOEXEC例外，并执行`/system/bin/sh /proc/self/fd/<fd>`。因此shell最终打开的是被pin的object；workspace pathname替换不改变执行对象。script child退出后worker关闭自己的pinned fd；raw-init_cred child不持有可执行fd。

current native profile decoder仍是旧wire物证，故本grammar在canonical profile-v3 closure完成前不可达；R5不借此修改frozen `profile/**`，详见§5。

### D6. Backend-private root-context exact contract

新增`session/root_context_prepare.{hpp,cpp}`，只在rooted worker、PI窗口之外运行；它不是通用root executor。旧`ops.cpp:309-413`是迁移数值的唯一source authority，冻结如下：

| 项 | exact contract |
|---|---|
| uid gate | `getuid()==0 && geteuid()==0`；否则prepare失败，不dispatch |
| best-effort prelude | 向`/sys/fs/selinux/checkreqprot`写ASCII `0\n`；失败只记diagnostic |
| policy source | `/sys/fs/selinux/policy`，每次attempt重新open/read；长度`24..67108864` bytes |
| header | bytes 0..15必须为`8c ff 7c f9 08 00 00 00 53 45 20 4c 69 6e 75 78`；bytes 16..23任意 |
| mutation | byte offset 23执行`value |= 0xc0`，其它bytes不得改变 |
| temp | root-script workspace内exclusive `.ghostlock_policy.<pid>.<nonce>` ordinary file，`0600`，worker独占，fsync后交给child，只在cleanup unlink |
| status proof | `/sys/fs/selinux/status` offset 12的little-endian `uint32_t sequence`，每次exec前后exact pread；after必须不同于before |
| executable | direct `/system/bin/load_policy`，argv恰为`load_policy <temp-path>`；无PATH lookup/shell |
| retry | 10 attempts；每个child 8,000 ms deadline，超时`SIGKILL`并wait；任何失败后sleep 2,000 ms，含第10次，以保留旧行为 |
| prepare success | child exit 0 且status sequence改变；仅此允许dispatch common script |
| enforce cleanup | script正常返回或prepare失败后，向`/sys/fs/selinux/enforce`写`1\n`，随后readback恰为`1`；失败=`cleanup FAILED/CLEANUP_INCOMPLETE` |
| iomem cache | RunHost创建稳定、非symlink、0700的`<host-root>/backend-state/43499-Glued`并作为`persistentStateDirectory`传给Flow；Flow固定`GHOSTLOCK_HOME`为该目录。worker best-effort写同目录`.ghostlock_iomem.new`：首行恰为`# <profile.uname_r>\n`，随后是`/proc/iomem`且必须含`System RAM`；fsync后atomic rename为`.ghostlock_iomem`并chmod 0644。下次run的现有`apply_iomem_cache()`从完全相同的`runtime_config.home_dir`读取；失败不改变prepare/dispatch/cleanup fact |

prepare失败且enforcing恢复成功映射exit 22；prepare失败且恢复失败映射exit 23；script已exec但enforcing恢复失败映射exit 24。所有fork/exec child都由worker wait；temp file在成功、失败、超时、exec失败路径均unlink。仍permissive不能升级成功。

### D7. Rooted child顺序、fixed report wire与stdio终结

正常链固定为：

```text
pre-dispatch settle -> main writes G exactly once -> closes command writer
-> rooted child closes command reader -> marks auxiliary fds CLOEXEC -> fork worker
-> raw parent closes uid writer, redirects 0/1/2 to /dev/null, parks
-> worker setsid, redirects 0/1/2 to /dev/null, verifies uid/euid, prepares root context
-> forks script child -> script child keeps only pinned script fd across exec
-> /system/bin/sh /proc/self/fd/N -> worker waits script
-> restore enforcing / cleanup -> write one report -> close report writer -> worker exits
-> carrier main exact-reads report -> stable exit
```

现有uid pipe在handoff阶段重新定型为single-reader/single-writer result channel：main独占`uid_read`；fork后只有worker持有`uid_write`；raw parent、script child和所有policy child都在各自边界关闭或CLOEXEC。固定POD恰为32 bytes，且`static_assert(sizeof<=PIPE_BUF)`：

```text
RootWorkerResultV1 = 8 x uint32_t
magic=0x31575247, version=1, byteSize=32,
primitive, dispatch, cleanup, hint, failure
primitive: 1=SUCCESS 2=FAILED 3=UNKNOWN
dispatch/cleanup: 0=SKIPPED 1=SUCCESS 2=FAILED 3=UNKNOWN
hint: 0=NONE 1=REBOOT_REQUIRED 2=UNKNOWN_HELD_STATE
failure: 0=NONE 1=UID 2=POLICY_PREPARE 3=SCRIPT_IDENTITY
         4=WORKER_FORK 5=SCRIPT_FORK 6=SETSID 7=EXEC
         8=SCRIPT_WAIT 9=POLICY_RESTORE 10=REPORT_CONTRACT
         11=W3_PARKED_FALLBACK 12=SCRIPT_EXIT_NONZERO
         13=SCRIPT_SIGNAL 14=EXEC_STATUS
```

当前唯一report writer（正常为worker；仅worker-fork失败前为raw parent）用一次blocking write发送完整32 bytes；只对`EINTR`在零字节前重试，partial写视为report failure，`SIGPIPE`固定ignore。main用monotonic 900,000 ms总deadline做poll+exact-read；short/partial、bad magic/version/size/enum、EOF-before-record、第二条record或valid record后1,000 ms内无EOF均映射exit 25。deadline后main关闭reader并退出25；detached worker继续，但它与script/manager均不持有carrier stdout/stderr或result reader。

worker在完成script identity recheck后、fork script child前创建唯一`pipe2(O_CLOEXEC)` exec-status pipe。worker独占read end；script child独占write end。fixed record恰为8 bytes且一次atomic write：`ExecStatusV1{kind:uint32_t,error:int32_t}`，`kind=1 SCRIPT_SETUP`或`2 EXEC`，error必须为`1..4095`。child任何受控pre-exec失败都调用唯一publisher：8-byte blocking write仅在零字节`EINTR`时重试；完整write后`_exit(127)`；zero/partial/其它error时立即`_exit(126)`，其中126是`EXEC_STATUS_PUBLISH_FAILED` reserved child exit。clear-CLOEXEC/argv setup失败写SCRIPT_SETUP，`execve`失败写EXEC；除此以外pre-exec代码不存在其它`_exit`路径。成功exec由CLOEXEC关闭write end。即使exec后的shell合法返回126，worker也保守映射UNKNOWN；canonical renderer正常尾部固定exit0，因此该保守冲突不升级为成功。

worker对status pipe用5,000 ms monotonic deadline：exact tagged record按kind映射`EXEC_STATUS`或`EXEC`；partial/bad kind/bad errno/read error/timeout=`EXEC_STATUS`，timeout时先`SIGKILL`。zero-byte EOF只表示“没有收到显式pre-exec failure”，必须与**successful waitpid status**组合；waitpid对EINTR无限重试至成功或其它error。只有`EOF + successful wait + WIFEXITED + exit!=126`可依据受控pre-exec不变量证明shell曾exec；`EOF + WIFSIGNALED`、`EOF + exit126`、任何waitpid error或其它wait status都固定fail-closed为UNKNOWN/code25。只有successful wait才可写child已reap。

pre-worker/early-worker失败的唯一owner与终结如下；所有分支均在900,000 ms前主动产出record或EOF，不靠deadline作为正常控制流：

| 分支 | report writer与record | cleanup | writer/stdio终结 | raw child | carrier code |
|---|---|---|---|---|---|
| worker fork失败 | raw parent写`primitive=SUCCESS, dispatch=FAILED, hint=REBOOT_REQUIRED, failure=WORKER_FORK` | raw parent直接restore enforcing；成功=`SUCCESS`，失败=`FAILED` | atomic report后close uid writer，再把0/1/2转`/dev/null` | park | 22或23 |
| worker `setsid`失败 | worker写`primitive=SUCCESS, dispatch=FAILED, hint=REBOOT_REQUIRED, failure=SETSID` | worker restore enforcing并写SUCCESS/FAILED | worker close writer与0/1/2后exit；raw parent早已关writer/stdio | park | 22或23 |
| worker uid/euid gate失败 | worker写`primitive=SUCCESS, dispatch=UNKNOWN, cleanup=UNKNOWN, hint=UNKNOWN_HELD_STATE, failure=UID`；此前W2的uid0观测仍保留primitive历史事实 | best-effort restore但不升级未知fact | worker close writer与0/1/2后exit；raw parent早已关writer/stdio | park | 25 |
| script fork失败 | worker写`dispatch=FAILED, hint=REBOOT_REQUIRED, failure=SCRIPT_FORK` | restore成功/失败决定cleanup | worker report后close writer/stdio并exit | park | 22或23 |
| worker fork前identity recheck失败 | worker不fork script，写`dispatch=FAILED, hint=REBOOT_REQUIRED, failure=SCRIPT_IDENTITY` | restore成功/失败决定cleanup | worker report后close writer/stdio并exit | park | 22或23 |
| exact tagged SCRIPT_SETUP/EXEC + successful wait | child已证明reap；写`dispatch=FAILED, hint=REBOOT_REQUIRED, failure=EXEC_STATUS或EXEC` | restore成功/失败决定cleanup | worker report后close writer/stdio并exit | park | 22或23 |
| exact tagged failure + waitpid error | tagged record仍证明dispatch FAILED，但child未证明reap；report以`failure=SCRIPT_WAIT`为主，tag kind只留diagnostic | `cleanup=UNKNOWN, hint=UNKNOWN_HELD_STATE`；仅best-effort restore | worker report后close writer/stdio并exit | park | 25 |
| exec-status partial/bad/timeout或EOF+reserved exit126 | timeout时kill；无论wait是否成功都不升级exec事实，写`dispatch=UNKNOWN, cleanup=UNKNOWN, hint=UNKNOWN_HELD_STATE, failure=EXEC_STATUS` | 仅best-effort restore | worker report后close writer/stdio并exit | park | 25 |
| status EOF + successful wait + script exit 0 | 由受控pre-exec invariant与非reserved status证明exec，写`dispatch=SUCCESS, failure=NONE` | restore成功=`SUCCESS`、失败=`FAILED` | worker report后close writer/stdio并exit | park | 0或24 |
| status EOF + successful wait + script exit 1..255且不等于126 | 同样证明exec；dispatch仍为SUCCESS，写`failure=SCRIPT_EXIT_NONZERO`，GLR1决定Payload/Finish产品结果 | restore成功/失败决定cleanup | 同上 | park | 0或24 |
| status EOF + successful wait + child被signal终止 | 无法区分pre/post-exec；写`dispatch=UNKNOWN, cleanup=UNKNOWN, hint=UNKNOWN_HELD_STATE, failure=SCRIPT_SIGNAL` | best-effort restore但不升级unknown | worker report后close writer/stdio并exit | park | 25 |
| status EOF + waitpid error/非terminal status | 未证明exec或reap；写`dispatch=UNKNOWN, cleanup=UNKNOWN, hint=UNKNOWN_HELD_STATE, failure=SCRIPT_WAIT` | best-effort restore但不升级unknown | worker report后close writer/stdio并exit | park | 25 |

fork成功是uid writer ownership的唯一transfer点：fork前raw child独占；fork后worker独占且raw parent立即close。若raw parent连failure record也无法完整write，它仍close writer/stdio并park，main通过short/EOF映射25。

`BackendProcessCarrier`在process退出后显式close Java stdout/stderr streams，并最多join collector各2,000 ms；仍未结束则返回typed `CollectorIncomplete`，43499映射UNKNOWN_HELD_STATE而不是永久等待。W3旧`P`路径已关闭command read端，因此frontend不再向它伪写`G`；只返回exit 25，不改W3攻击算法。

### D8. 稳定 process exit → exact typed facts

不新增GLC sidecar。下表中的stage写法均为`state/reason/exitKind/exitValue`，是Kotlin `Cve43499CarrierExit`唯一mapping authority：

| exit/observation | primitive | dispatch | cleanup | hint | GLR1 |
|---|---|---|---|---|---|
| 0 | `SUCCESS/NONE/NONE/0` | `SUCCESS/NONE/NONE/0` | `SUCCESS/NONE/NONE/0` | `REBOOT_REQUIRED` | 必须decode |
| host readiness拒绝（未spawn） | `FAILED/NOT_READY/NONE/0` | `SKIPPED/NOT_STARTED/NONE/0` | `SKIPPED/NOT_STARTED/NONE/0` | `NONE` | 不读 |
| 20 native pre-attack reject/DiagnosticStop | `FAILED/REJECTED/NATIVE_STATUS/20` | `SKIPPED/NOT_STARTED/NONE/0` | `SKIPPED/NOT_STARTED/NONE/0` | `NONE` | 不读 |
| 21 known attack failure且证明无park/transfer | `FAILED/PROCESS_EXIT_NONZERO/PROCESS_EXIT/21` | `SKIPPED/NOT_STARTED/NONE/0` | `SKIPPED/NOT_STARTED/NONE/0` | `NONE` | 不读 |
| 22 rooted、dispatch失败、enforce恢复成功 | `SUCCESS/NONE/NONE/0` | `FAILED/PROCESS_EXIT_NONZERO/PROCESS_EXIT/22` | `SUCCESS/NONE/NONE/0` | `REBOOT_REQUIRED` | 不读 |
| 23 rooted、dispatch失败且cleanup失败 | `SUCCESS/NONE/NONE/0` | `FAILED/PROCESS_EXIT_NONZERO/PROCESS_EXIT/23` | `FAILED/CLEANUP_INCOMPLETE/NONE/0` | `REBOOT_REQUIRED` | 不读 |
| 24 rooted、dispatch成功、cleanup失败 | `SUCCESS/NONE/NONE/0` | `SUCCESS/NONE/NONE/0` | `FAILED/CLEANUP_INCOMPLETE/NONE/0` | `REBOOT_REQUIRED` | 必须decode |
| 25 rooted后report缺失/坏/W3 parked fallback | `SUCCESS/NONE/NONE/0` | `UNKNOWN/INTERNAL_CONTRACT_FAILURE/NONE/0` | `UNKNOWN/INTERNAL_CONTRACT_FAILURE/NONE/0` | `UNKNOWN_HELD_STATE` | 不读 |
| spawn I/O failure | `FAILED/IO_FAILURE/NONE/0` | `SKIPPED/NOT_STARTED/NONE/0` | `SKIPPED/NOT_STARTED/NONE/0` | `NONE` | 不读 |
| Java可见exit 1..19或26..255（含可能的128+signal；不反推signal） | `UNKNOWN/INTERNAL_CONTRACT_FAILURE/NONE/0` | 同左 | 同左 | `UNKNOWN_HELD_STATE` | 不读 |
| collector incomplete | `UNKNOWN/INTERNAL_CONTRACT_FAILURE/NONE/0` | 同左 | 同左 | `UNKNOWN_HELD_STATE` | 不读 |

code 21只允许在session能证明没有rooted child transfer/park时产生；否则必须25。exit 0也留下借用static `init_cred`的parked raw child，所以hint不能是NONE；raw uid0只形成primitive fact。产品结果仍由GLR1与§7.3优先级合成，正常路径会因held child得到`REBOOT_REQUIRED`，不会冒充`RunTerminal.SUCCESS`。

### D9. GLR1、未开始items与terminal compose

`RunHostProductPipeline`把同一个immutable `RootScriptExecutionPlan`实例/值交给renderer、GLR1 decoder与composer；不存在由domain `PayloadDefinition`/`FinishDefinition`伪造的第二expectation。其normalized expectation规则为：

1. dispatch非SUCCESS时不读空result；每个requested Payload/Finish item合成为`NOT_RUN/NOT_STARTED/NONE/0`，两个overall均为`SKIPPED/NOT_STARTED/NONE/0`。
2. dispatch SUCCESS时必须读取同一pre-created result object；identity replacement=`ARTIFACT_IDENTITY_MISMATCH`，partial/malformed/out-of-order=`MALFORMED_RESULT`。
3. 完整GLR1按frozen order映射；required Payload fail-fast与Finish best-effort完全沿用父wire §7.2，不修补partial records。
4. exit 0/24之外的carrier fact不得以stdout、log或existing file补猜GLR1。
5. durable handoff优先`REBOOT_REQUESTED`；park/held hint其次`REBOOT_REQUIRED`；cleanup/safety failure、UNKNOWN、FAILED、DEGRADED依父wire唯一优先级合成。

### D10. Cancellation、host death、process termination与retention

- PREPARED前可取消；RUNNING后`BEFORE_START_ONLY`拒绝cancel，不调用`Flow.stop()`。
- `BackendProcessRequest`新增typed termination policy；43499固定`NEVER_KILL_AFTER_START`且`timeoutMs=0`。generic killable caller保留原行为，43499不执行`destroy/destroyForcibly`。
- host/Binder death后不得自动重试；setsid worker继续script/restore，client合成UNKNOWN/HOST_DIED。
- main自己的900,000 ms report deadline只结束carrier observation，不kill worker；结果固定exit25/retained workspace。
- `UNKNOWN`、`NEEDS_ATTENTION`、`REBOOT_REQUIRED`与host death保留workspace；只有无held-state的明确prestart/attack failure可按既有policy回收。

## 5. Canonical profile-v3 阻断

current `RunInputFreezer`发布的只是 R3 diagnostic fixture，而 native仍消费旧 binary profile。R5可以完成Disabled declaration、opened-object lease、host pipeline与native carrier接口，但必须保持backend Disabled，且不得宣称R5 carrier source complete，直到另一个获批profile-v3 closure同时提供：

```text
用户选择的 canonical GLK profile v3 JSON/JCS
-> one freeze boundary
-> one backend-private 43499 wire encoder
-> staged PROFILE ordinary file
-> native profile/** decoder accepts only that version-three-derived backend wire
```

本计划明确禁止以下“临时打通”：

- 直接把 bundled `schema_version=1` HOCON当运行时authority；
- 恢复旧 `Glued43499ProfileCodec`；
- 修改或绕过 `LegacyProfileConverter.kt`；
- 把 offsets写成 Kotlin/C++ compile constants；
- 让 Flow读任意外部path或环境变量。

该closure会另行设计并修改本计划冻结的`profile/**`；R5不先造临时encoder/parser。当前批可以建立不可达的Disabled plugin与carrier骨架，但registry selectable、readiness、carrier source complete与device support checkbox保持未完成。

## 6. 所有权与生命周期

| 对象 | 创建/owner | 借用者 | 终结点 | 失败/host death |
|---|---|---|---|---|
| source master PFD | `FrozenRunInputLease` | request assembler只duplicate | 全部request duplicates完成即close | partial assembly关闭duplicates与全部masters |
| request duplicate PFD | `BackendRunSessionAdapter` | Binder/RunHost stage | client调用结束finally close | host拒绝/异常同样close |
| staged profile/carrier | RunHost workspace | Flow/process carrier | terminal retention decision | UNKNOWN保留 |
| `RootScriptArtifact` | RunHost publisher | Flow/native rooted worker | GLR1 parse + retention decision | object mismatch拒绝并保留 |
| pinned profile fd | native main | profile decoder通过`/proc/self/fd/N`借用 | decode完成立即close | mismatch/parse fail→exit20 |
| pinned script fd | native main/session | victim→worker→script child | script child退出后worker close；raw child不保留 | mismatch在attack前拒绝；exec child仅此fd清CLOEXEC |
| `VictimContext` fds/child | `ExploitSession` | W2/W3/frontend | retire或显式transfer | 不隐式double-close |
| worker | rooted child创建；worker自有session与唯一report writer | policy/script children | wait script + cleanup + atomic report + close writer后exit | main deadline后继续；SIGPIPE ignore，不自动retry |
| raw-init_cred child | victim/frontend | 无普通借用者 | park至reboot | fork worker后立即关report writer并把0/1/2转`/dev/null` |
| policy temp file | root-context prepare | `load_policy` child | cleanup unlink | cleanup失败→REBOOT_REQUIRED |
| GLR1 result object | RunHost预创建，root script写 | codec读 | parse完成/retention | replacement/partial→UNKNOWN |

有意UAF只限漏洞目标；script fd、pipe、worker report、policy buffer、temp file与result object都不得成为辅助UAF。

### 6.1 AGENTS §3 实施授权边界

critic R5-09指出：generator、policy、handoff与process lifecycle属于语义替换，不能冒充机械改动。正常§3流程要求先注释保留旧码，判据全绿并确认后同批删除；而用户本轮又要求所有验证`NOT_RUN`，两者无法同时满足。

因此当前精确边界是：

- `handoff_probe.*`及其caller/test/fixture在caller清零后属于机械DELETE，可直接删除并在新码补标准注释；
- 其它S1/S2/S3 production source一律不在R5-D0后启动，直到用户明确知情确认：“本R5 ledger内语义替换允许直接修改、不套旧码；接受本轮无验证造成的错误、panic与后续返工风险”；
- 若用户给出该确认，同批沿革登记为越过AGENTS §3的套旧码与全绿删除条件，owner=`root-execution/R5`，偿还触发=`下一次允许验证时或任何enable/selectable/release动作前，以较早者为准`；所有source checkbox仍只写`changed / NOT_RUN`，不写PASS；
- 若未给确认，则恢复正常验证+确认流程，不能把临时旧码块跨批留下。

## 7. 批次与实时 checkbox

### R5-D0：设计与 exact scope

- [x] current-tree Kotlin/native ownership inventory。
- [x] `git log --all -- <path>`历史定位。
- [x] active-plan重复/冲突判定：本文件只承接父计划R5。
- [x] 写入本计划与exact ledger。
- [x] design-critic首轮冻结快照复评：9个阻断，均已逐条设计回应。
- [x] 把首轮critic逐条回应写入§10。
- [x] design-critic第二轮冻结快照复评：3个阻断、1个重要，均已逐条修订。
- [x] design-critic第三轮冻结快照复评：1个阻断、1个重要，均已逐条修订。
- [x] design-critic第四轮冻结快照复评：1个阻断，已补tagged exec-status与pre-exec signal fail-closed语义。
- [x] design-critic第五轮冻结快照复评：1个阻断，已补status发布失败reserved exit与waitpid error组合。
- [x] design-critic第六轮定稿：阻断=0、重要=0、建议=0，可进入批准gate。
- [x] 2026-11-05 用户以“批准R4A和R5”批准本计划；该批准覆盖§6.1已冻结的R5 ledger语义替换例外、无旧码注释块与本轮门禁`NOT_RUN`代价。owner=`root-execution/R5`；偿还触发仍为下一次允许验证或任何enable/selectable/release动作前的较早者。
- [x] 2026-11-05 R5-S1实施前定点核对发现codec exact-scope漏项；child ledger v3增加`RootScriptResultCodec.kt`与既有test的两条`MODIFY`，删除伪domain expectation。该勘误不改wire/AIDL/schema/route，沿用既有批准，critic结论为阻断=0；门禁仍为`NOT_RUN`。

### R5-S1：common product host closure

- [x] 建neutral carrier declaration、`FrozenRunInputLease`与published Flow artifact catalog（source changed；`NOT_RUN`）。
- [x] source resolver从同一opened object做fstat/digest/PFD duplicate；删除pathname reopen（source changed；`NOT_RUN`）。
- [x] stager按kind固定mode并核同一request PFD bytes（source changed；`NOT_RUN`）。
- [x] App/Shizuku共用product publish/decode/compose pipeline，删除两处 PRODUCT_ROOT `NOT_READY` stub（source changed；`NOT_RUN`）。
- [x] renderer/decoder/composer收敛到同一个`RootScriptExecutionPlan`，接通GLR1与six-layer snapshot；不从日志推断（source/test source changed；`NOT_RUN`）。
- [x] ROOT_MANAGER mandatory executable PFD + actual argv接入；删除renderer目录搜索/fallback（source/test source changed；`NOT_RUN`）。

### R5-S2：43499 native carrier

- [x] **CANCELLED**：不修改 43499-Glued 的 argv、root worker、policy、handoff、waiter/race、route、park 或测试实现。
- [x] 已撤销本轮曾暂存的 `root_context_prepare.*`、`root_script_dispatch.*` 及其它 R5-S2 局部编辑；保留旧 backend source 本体不变。
- [x] 不单独删除仍被旧 backend 调用的 `handoff_probe.*` closure；在保留 source-only candidate 的前提下，删除它而不改 caller 会制造不可构建残树。

### R5-S3：Disabled plugin纵向接线

- [x] **CANCELLED**：不增加 `43499-Glued` spec、plugin、Flow 或 registry membership。
- [x] CMake backend assembly 已删除全部 real-attack option/include；Gradle 已删除 backend flags、M0 source staging/build task 与 marker wiring。
- [x] `BackendRegistry` 保持零真实攻击 backend：仅 disabled CUSTOM 与 debug-only DIAGNOSTIC probe。
- [x] UML、backend README、introduction、MASTER/REMAINING/ROADMAP 已同步为 disconnected/source-only 状态。

### R5-V：验证与启用（用户本轮要求不执行）

- [x] R5 carrier gates 因实施取消而不再是待办；未运行项仍记 `NOT_RUN`，不写成 PASS。
- [x] selectable/Ready/supported 保持不存在；断开状态不得解释为通过任何攻击门。

## 8. 验证矩阵（只冻结判据，不在本轮执行）

| 层 | 正向 | 负向/必须能失败 | 本轮 |
|---|---|---|---|
| source/request/staging | one open→fstat/digest→PFD dup→staged identity | pathname replacement、symlink、wrong digest/kind、caller mode/path | NOT_RUN |
| product pipeline | exact script publish→Flow→GLR1→snapshot | partial、duplicate、replaced result、dispatch-only | NOT_RUN |
| normalized renderer | mandatory manager executable + exact argv | root-side find/fallback、launchOnly、KMI parse fail | NOT_RUN |
| process carrier | staged ET_EXEC、NEVER_KILL、bounded stream close/join | direct nativeLibraryDir、running kill、collector held by descendant | NOT_RUN |
| native argv/object | pinned profile+script metadata，`/proc/self/fd/N` exec | old flags、duplicate、path replacement、metadata mismatch | NOT_RUN |
| root context | D6 header/byte23/retry/sequence/enforcing exact chain | invalid header、timeout、false-success sequence、restore fail | NOT_RUN |
| report/exit mapping | 32-byte wire + 0/20-25 exact typed table | pre-worker failure、short/duplicate/no EOF/deadline/noncanonical Java exit | NOT_RUN |
| lifecycle | settle→G→fork→redirect→prepare→fd-exec→wait→restore→report→park | reorder、double close、raw/manager持有collector fd | NOT_RUN |
| target device | cold boot/fixed CPUs/single route/KSU absent | warm run、AVD替代、stdout替代GLR1 | NOT_RUN |

## 9. Frozen paths

R5 不得修改：

- `backends/43499glued/kernel/**`
- `backends/43499glued/kernelsnitch/**`
- `backends/43499glued/memory/**`
- `backends/43499glued/profile/**`
- `backends/43499glued/race/**`
- concrete route算法与route controller/middleware
- `session/victim_context.*`
- `support/native_resource.*`
- `LegacyProfileConverter.kt`
- 43284-Glued、98260 native trees

`session/backend/cve_2026_43499_backend.cpp`只允许删除setup中的product-script generator调用与随之失去用途的参数；W1/W2/W3、write/race主体冻结。

## 10. Design-critic 记录与逐条回应

首轮冻结快照：`2026-11-05T06:51:39Z`，Git `21d9f53c00ee76891a993c8a2ecea1ea6f9be830`；总判=`仍需修订`，阻断数=9。审查期间无writer覆盖该快照；Gradle/CMake/CTest/AVD/Shizuku/真机均按用户要求`NOT_RUN`。

| 编号 | 严重度 | 回应 | 修改位置 | 状态 |
|---|---|---|---|---|
| R5-01 | 阻断 | 撤销optional manager artifact与`[managerName,safeMode]`暗合同；严格沿父wire使用mandatory executable artifact和actual argv，删除root-side lookup | D3-D4；ledger删除`RunHostRequest.kt`改动并增加renderer/resolver closure | 待复评 |
| R5-02 | 阻断 | `sourceToken`改opaque lease token；一次open后同一fd做fstat/digest，wire只dup master；AppContainer注入resolver并冻结close/delete所有权 | D2、§6；ledger增加AppContainer/FrozenRunInputLease/FrozenRunInput closure | 待复评 |
| R5-03 | 阻断 | main pin script fd，script child只保留该fd跨exec并用`/proc/self/fd/N`交给shell，禁止verified-open后path-exec | D5、D7、§6 | 待复评 |
| R5-04 | 阻断 | 冻结32-byte report及全部worker/script分支；第四轮补tagged exec status；第五轮把status write失败固定reserved exit126，并要求successful wait才能证明reap/exec，tag+wait error与EOF+wait error均UNKNOWN/code25 | D5、D7、D10 | CLOSED（第六轮） |
| R5-05 | 阻断→第二轮重要 | 把旧policy exact合同冻结；第二轮再把iomem writer固定到与reader同一persistent `GHOSTLOCK_HOME`，补release stamp、owner与atomic rename | D6 | 待第三轮复评 |
| R5-06 | 阻断→第三轮重要 | 为0/20-25、spawn、unknown exit、collector incomplete列完整合法StageResult；把host NotReady独立并删除Java signal推断；第三轮同步ledger删除signal observation字样 | D8-D9、child ledger v2 | 待第四轮复评 |
| R5-07 | 阻断 | 固定唯一argv grammar；删除stdin/status/force/debug flags，补齐route/backend caller closure；profile-v3 parser继续作为独立enable blocker | D5、§5；ledger native caller rows | 待复评 |
| R5-08 | 阻断 | 重生成exact ledger；第二轮再冻结`BackendPlugin.carrierArtifact=null`、`BackendFlowInput.artifacts=emptyMap()`与nullable state directory constructor defaults，既有constructor无需伪数据或额外caller改动 | D1-D2、child ledger v2 | 待第三轮复评 |
| R5-09 | 阻断 | 不把“不要验证”解释为§3例外；计划显式暂停semantic production edits，等待一次范围明确的知情强制确认 | §6.1、R5-D0 | 等待用户裁定 |

第二轮冻结快照：`2026-11-05T07:17:34Z`，Git同上；总判=`仍需修订`，阻断=3（R5-04/06/08）、重要=1（R5-05）。R5-01/02/03/07/09由critic确认CLOSED；R5-09只是设计关闭，不构成用户授权。上述第二轮问题现已修订，等待第三轮定点复评。

第三轮冻结快照：`2026-11-05T07:31:18Z`，Git同上；总判=`仍需修订`，阻断=1（R5-04）、重要=1（R5-06 ledger文案）。critic确认R5-05/08关闭。现已增加exec-status pipe与script所有wait-status分支，并同步ledger文案，等待第四轮定点复评。

第四轮冻结快照：`2026-11-05T07:35:01Z`，Git同上；总判=`仍需修订`，阻断=1（R5-04）、重要=0。R5-06已关闭。现已让worker先做identity recheck，把exec-status改为带kind+errno的8-byte record，并把EOF+WIFSIGNALED固定UNKNOWN/code25，等待第五轮定点复评。

第五轮冻结快照：`2026-11-05T07:37:31Z`，Git同上；总判=`仍需修订`，阻断=1（R5-04）、重要=0。现已为exec-status发布失败保留child exit126，并把exact tag/EOF与successful wait、wait error、signal、reserved exit逐格组合；任何未证明reap/exec的路径均UNKNOWN/code25，等待第六轮定点复评。

第六轮冻结快照：`2026-11-05T07:39:46Z`，Git同上；总判=`可进入批准 gate`，阻断=0、重要=0、建议=0。R5-01..08全部CLOSED；R5-09在设计层CLOSED，但critic不代替§6.1用户授权。

S1 exact-scope勘误复评：`2026-11-05`；总判=`可作为已批准R5的必要闭包勘误`，阻断=0、重要=3、建议=1。R5S1-E01/E02要求codec直接消费同一个`RootScriptExecutionPlan`并删除有损domain reconstruction；R5S1-E03要求child ledger v3加入codec及既有codec test两条`MODIFY`；R5S1-E04要求记录本沿革。该勘误不新增产品行为、wire字段、AIDL、profile schema或execution domain；实现与测试门禁仍为`NOT_RUN`。

当前结论：**R5设计与exact ledger已定稿；production implementation仍未开始，唯一下一门是§6.1用户明确知情授权。**
