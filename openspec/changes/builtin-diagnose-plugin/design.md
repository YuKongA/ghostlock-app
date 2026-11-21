# BXP3-B1 builtin Diagnose vertical

## Context and scope

完整迁入的有效设计/范围与验收正文；不是新增实施批。原批准/历史结果按原Beads记录，工具工件齐备不代表实现或门禁通过。

来源：`043fdf6a:.beads/issues.jsonl`；保留语义代号和原锚点。本文中的历史进度/门结果文字仅是其具名输入的沿革，当前状态从proposal列出的Beads读取。旧机制由正文已注明的后继差异取代；基线不重新授权实施。

## Decisions

<a id="record-gl-dek"></a>

## P-BACKEND-X-002：BXP3-B1 剩余 source implementation

<!-- migrated-source: 043fdf6a Beads gl-dek description -->
本节是迁入OpenSpec的现行设计/验收正文；Beads只保存执行状态、原批准和实际证据。历史来源仅作追溯。
2026-11-20生产批准保留；scope仅§19.5.70的148行。2026-11-21用户明确把AGP现行唯一版本固定为`9.1.0`，旧AGP `9.4.1` P0只保留为历史证据，不能作为当前门PASS；现有source不回退成空模块重演旧实验，改在当前树执行同命令的`9.1.0`工具链复验。当前执行状态与依赖只读Beads。不接provider/import/Custom/真实backend，不用旧240行。
原始来源（已归档/承接）：docs/archive/20261121-1301-backend-root-execution-unification-plan.md#p-backend-x-002-decision-03-batch-01

### 提交基线与 build hunk owner 修订（2026-11-21）

本批源码候选必须以已独立验证的 [E-BUILD-X-002 Allure 报告门基线](../allure-reporting-gates/design.md#record-allure-reporting-gates) 为 parent；随后才由 `gl-4on` 增加剩余构建闭环清理。三棵候选的逐路径／hunk唯一 owner与临时 index冻结方法以该设计为准，取代“只反转 Beads 依赖即可提交”的旧假设，但不改变本节148行production scope、AGP `9.1.0`裁定或产品合同。

`gl-dek` 必须整文件拥有 `plugin/packages/diagnose/build.gradle.kts` 的最终字节，并拥有 `nativeTestContractsHost` 的 plugin-owned source inputs、tools CMake／Diagnose ABI include、App plugin asset／boundary producer及 `nativeBuildAndroid` plugin总入口。它不得吸收 Allure task／bridge／报告规范，也不得吸收旧 ONDK／profile／LKM／CI／`checkAll` 清理。`gl-4on`可以复验前述 producer，但不再拥有这些前驱源码。该划分使 `gl-dek` 候选本身可构建，且避免同一未跟踪 build script 在两个提交间出现无定义中间态。

同一owner同时负责把`engineering-standards.md`的`nativeBuildAndroid`当前命令和`full-process-uml.md`的plugin build topology／gate节点同步到上述B1事实；`gl-4on`随后只删除`checkAll`和其它失效构建边，不得把该命令降回裸debug/release assemble。Allure候选此前只更新报告链、去预写结果数字，并保留HEAD当时仍存在的旧native topology／`nativeBuildAndroid`／`checkAll`。

<!-- migrated-source: design gl-dek -->
<!-- source: f882e670:docs/plan/backend-root-execution-unification-plan.md:L4408-L4838 -->
<a id="p-backend-x-002-decision-03-batch-01"></a>

###### P-BACKEND-X-002/决策03/批次01：builtin Diagnose package/catalog 最小纵切（L级 exact design；production已批准；当前状态只读Beads）

> 状态沿革：本节只细化决策03获认可后的第一个真实vertical。目标不是一次实现完全体，而是把现有release-visible Diagnose从same-APK静态`BackendRegistry`搬成一个由APK携带、首次启动安装到唯一catalog、经PFD传给App／Shizuku RunHost、在one-shot进程内动态加载、由插件Kotlin直接拥有JNA／private DSO的builtin `.glplugin.zip`。用户在production批准前纠正封装合同：扩展名必须明示ZIP；普通ZIP重排／改timestamp／改压缩级别不得改变plugin逻辑身份；manifest改为除自身外全部regular file共用SHA-256表并允许multidex；删除64 MiB／64 entry／128 KiB等格式级固定上限；whole-ZIP SHA-256与byteCount不再进入index、catalog或wire，ZIP CRC只负责容器意外损坏，安全完整性仍由APK认证的semantic content root与manifest逐文件SHA-256承担。该差异不引入Merkle tree、第二installer或第二identity authority，但实质改变manifest、catalog identity、wire及验收，故旧`BXP3-B1-C`结论不覆盖。作者差异自检和同一非作者critic定点复审均已完成，critic最终阻断／重要／建议／新增finding均为0；用户随后于2026-11-20明确批准本批production implementation。旧`BXP3-B1-P0`曾在Gradle 9.8.1／Kotlin 2.4.20／AGP 9.4.1的空API／最小core输入上通过，只作为当时输入的历史证据。用户于2026-11-21明确把AGP现行唯一版本固定为`9.1.0`；由于148行source已经存在，当前不删除或掏空模块重演旧实验，而是在现有工作树用同一命令复验Gradle 9.8.1／Kotlin 2.4.20／AGP 9.1.0的Android＋JVM＋App consumer与configuration-cache闭包。复验前保持`NOT_RUN`，其余产品门也不得提前记为PASS。

###### 19.5.68 现状与基线

设计基线固定为branch=`dev`、HEAD=`4996d25cee32794e0631a8401104820f253ca95b`，并以2026-11-19工作树中已经形成但尚未提交的`P-RUN-X-001/批次03-04` owner拆分为真实调用者；不得退回旧`AppRunHostService`／`GhostlockUserService`或抹除他人改动。本批Explore得到的直接事实为：

1. `settings.gradle.kts`目前只含`:app`；`:app`同时编译Diagnose Kotlin、JNA mapping、C/C++，直接依赖JNA AAR并把`libghostlock_diagnose.so`放入普通APK native graph。Host因而仍是backend code/native的build与runtime owner。
2. `BackendRegistry`是same-APK静态membership；main `AppContainer`、App RunHost与Shizuku RunHost分别直接读取它。`BackendDefinition`还把descriptor、runtime和Android Compose UI绑在同一个outer object，不能表示独立安装的exact package。
3. `DiagnoseFlow → DiagnoseNativeBinding → Native.load("ghostlock_diagnose", …)`已经证明真实JNA／native／shell handoff链，但这些实现都位于`:app` classloader；`DiagnoseNativeBinding.close()`只关闭pipe，不也不能证明DSO卸载。
4. current typed wire只有`backendKey/profile/payload/finish/artifacts`，Diagnose仍被迫生成并运输一个它不消费的profile-v3临时文件；RunHost没有exact package object identity或archive PFD。
5. 两个client在终态后调用`requestClose()`，但没有调用现有AIDL `destroy()`。App service的`destroy()`即使被调用也只`stopSelf()`；该动作不等于`:flowhost`进程死亡，故不能作为classloader／DSO版本切换边界。Shizuku虽为每run unique process suffix，当前client同样没有完成“请求销毁→观察binder death”握手；B1沿用Shizuku对`daemon(false)`的client-death合同，不另建Host watchdog。
6. main process已经只在`AppContainer.initialize()`完成后展示UI，可以把builtin seed置于这一唯一初始化链，而不在`Application.onCreate()`同步做文件I/O。catalog失败必须返回可展示的`FAILED`状态，不能让splash永久等待。
7. minSdk=31覆盖公开[`DelegateLastClassLoader`](https://developer.android.com/reference/dalvik/system/DelegateLastClassLoader)；targetSdk=37使动态DEX在首次load前必须满足[Android 14起的只读要求](https://developer.android.com/about/versions/14/behavior-changes-14)。Android官方同时要求[DCL使用受信内部位置并在load前做完整性核验](https://developer.android.com/privacy-and-security/risks/dynamic-code-loading)。本批把这些要求变成archive staging与device gate，不把“能构造classloader”写成已经兼容。
8. AGP 9下KMP shared library应使用`org.jetbrains.kotlin.multiplatform`加[`com.android.kotlin.multiplatform.library`](https://developer.android.com/kotlin/multiplatform/plugin)，[Android application entry仍应独立](https://kotlinlang.org/docs/multiplatform/multiplatform-project-agp-9-migration.html)；该KMP Android library plugin不承载AIDL或`externalNativeBuild`。因此本批只让纯manifest/catalog模型进入`:plugin:core` KMP，Diagnose Android＋C++叶仍用独立`:plugin:packages:diagnose` Android library，`:app`继续作为Android application壳。

真实consumer链固定为：

~~~mermaid
flowchart LR
  APK["APK builtin index + .glplugin.zip asset"]
  SEED["AppContainer.initialize<br/>seed / verify / immutable install"]
  CAT["PluginCatalog READY snapshot<br/>唯一installed membership"]
  UI["Host descriptor + Diagnose presentation projection"]
  CUT["run cutoff<br/>semantic content root + archive PFD"]
  RH["App or Shizuku one-shot RunHost"]
  LOAD["verify / read-only extract / classload"]
  PLUG["Diagnose plugin Kotlin<br/>JNA + private DSO"]
  TERM["typed diagnostic result + process death"]

  APK --> SEED --> CAT
  CAT --> UI
  CAT --> CUT --> RH --> LOAD --> PLUG --> TERM
~~~

###### 19.5.69 目标与约束

**本批承诺**只有以下闭包：

- 建立`:plugin:api` Java-only shared ABI、`:plugin:core` Android/JVM KMP纯模型、`:plugin:packages:diagnose` Android code/native叶和`plugin/native-source/common` source-only公共源码authority；模块都位于统一`plugin/`子树，`.so`、DEX、staging与`.glplugin.zip`只出现在各module `build/`。
- 生成一个thin `android-arm64-v8a` Diagnose archive；APK只携带generated asset/index，`:app`的compile/runtime classpath不得包含Diagnose实现、JNA或Diagnose DSO。
- 首次可用启动由main process把builtin archive经同一strict verifier与immutable installer写入唯一catalog；catalog READY后才恢复backend selection与开放run。
- main只读manifest descriptor与Host presentation mapping，不加载plugin code；run cutoff冻结exact object并用read-only PFD传给两种RunHost。
- RunHost独立重验、只读提取、动态加载一个root BACKEND component；Diagnose entry直接构造同包binding，插件Kotlin而非Host调用JNA／DSO并返回typed diagnostic facts。
- 每个run结束、prepare拒绝或main/client死亡后都物理终止相应RunHost process；下一run不得复用装过plugin code／DSO的PID。
- 保持现行Diagnose status、uid/pid/nonce、native/shell两路日志、handoff、cleanup与`PASS/FAIL/UNKNOWN`派生语义，不把诊断成功冒充root产品成功。

**本批明确不承诺**：

- 不实现外部文件选择、第三方签名／信任UI、联网仓库、update channel或卸载；只把imported未来接入点限制在trust admission之前，不能用假import测试宣称已支持外部插件。
- 不实现`PROVIDER`／Document／Artifact job、依赖slot、runtime service registry、Custom capability directory、raw memory、43499、43284／KO、98260或extractor consumer。
- 不改现有profile wire与`ProfileSnapshot`全链：Diagnose在B1仍由Host生成、staging但plugin API不可见的profile-v3 fixture。这是已具名的过渡债，不是plugin需求；等data-only profile provider/Document真实consumer批再取消，不为本批顺手把profile改nullable。
- 不迁移plugin settings、统一i18n、Compose Multiplatform App或Desktop壳；本批只保留现有Host `DiagnoseBackendUi`作为由authenticated backend key选择的presentation adapter，它不是membership authority。
- 不发布ARMv7／x86／x86_64 archive；格式允许以后新增thin variant，但本批只构建、安装和验证当前真实arm64 target。
- 不声明热升级、同进程卸载或恶意插件沙箱；隔离目标是identity、意外链接、Host UI进程与lifecycle，不是阻止已获同UID／shell权限的code影响系统。

**exact module与依赖方向**：

~~~text
plugin/
  api/                         :plugin:api
    src/main/java/             Java-only shared ABI
  core/                        :plugin:core
    src/commonMain/            manifest / catalog pure models + strict codec
    src/commonTest/
    targets: androidLibrary + jvm
  native-source/common/        non-module, non-runtime, no shared DSO
  packages/diagnose/           :plugin:packages:diagnose
    src/main/kotlin/           entry / run / JNA binding / pipe collector
    src/main/cpp/              Diagnose C/C++ source and CMake
    src/main/plugin/           source manifest + redistribution notices
    build/                     AAR / raw DSO / DEX / staging / .glplugin.zip
app/                           :app Android application + Host/RunHost
  build/generated/assets/      builtin index + .glplugin.zip copy for APK
~~~

依赖固定为：`:app implementation(:plugin:api, :plugin:core)`；`:plugin:core`只引入与current App同版的KMP `kotlinx-serialization-json:1.12.0`；`:plugin:packages:diagnose compileOnly(:plugin:api)`并私有依赖JNA AAR与Kotlin runtime。App只通过一个variant-aware outgoing artifact configuration消费Diagnose唯一builtin `.glplugin.zip`，debug/release App不各自重建plugin object，也不建立`implementation`／`runtimeOnly` Diagnose project edge。ZIP字节无需canonical或reproducible；唯一producer只保证两个App variant消费同一已构建artifact。`:plugin:core`不依赖`:app`或Diagnose；`:plugin:api`不依赖Kotlin、Android、JNA、coroutine、serialization、Compose或其它project。

**`:plugin:api` revision 1全部public surface**位于`com.ghostlock.plugin.api.v1`。除Java编译器生成的enum `values/valueOf`、`Object/Throwable`继承member外，未列出的public/protected type、constructor、method和field均拒绝；所有interface在v1内不得新增abstract method，新增能力用新interface，破坏性变化进`api.v2`：

| type | exact public contract |
|---|---|
| `PluginApi` | final/private constructor；只公开`public static final int MAJOR=1`、`public static final int MINOR=0`。 |
| `PluginFailureCode` | enum恰为`INVALID_INPUT, NOT_READY, IO_FAILURE, CANCELLED, CONTRACT_FAILURE`。 |
| `PluginException` | checked exception；只公开`PluginException(PluginFailureCode code, String detail)`、`PluginFailureCode code()`、`String detail()`；detail为single-line printable UTF-8且`<=4096` bytes。 |
| `DiagnosticBackendEntryPoint` | interface只含`DiagnosticBackendRun createRun() throws PluginException`；manifest entry class必须有public无参constructor，constructor本身resource-free。 |
| `DiagnosticBackendRun` | interface只含blocking one-shot `DiagnosticRunResult execute(DiagnosticRunContext context) throws PluginException`与`void close() throws PluginException`；不继承`AutoCloseable`，首批没有`requestStop`，因为Diagnose的abort policy仍为`BEFORE_START_ONLY`。 |
| `DiagnosticRunContext` | interface只含`String runId()`、`long generation()`、`int processUid()`、`int processPid()`、`boolean cancellationRequested()`、`void emit(PluginLogOrigin origin, PluginLogLevel level, String message)`；`emit`不抛checked exception，delivery失败只由Host记drop；不暴露Android `Context`、filesystem path、classloader、registry、JNA或Host object。 |
| `DiagnosticRunResult` | final；只公开`DiagnosticRunResult(PluginStageResult backendPrimitive, PluginStageResult rootScriptDispatch)`、`PluginStageResult backendPrimitive()`、`PluginStageResult rootScriptDispatch()`；两值非null。cleanup由Host根据`close()`事实产生，outcome由Host按现行优先级派生。 |
| `PluginStageResult` | final；只公开`PluginStageResult(PluginStageState state, PluginResultReason reason, PluginExitKind exitKind, int exitValue)`及同名四个accessor；constructor拒绝下述表外tuple。 |
| enums | `PluginStageState={SUCCESS,FAILED,CANCELLED,UNKNOWN,SKIPPED}`；`PluginResultReason={NONE,NOT_STARTED,REJECTED,NOT_READY,IO_FAILURE,PROCESS_EXIT_NONZERO,SIGNALLED,CANCELLED,MALFORMED_RESULT,ARTIFACT_IDENTITY_MISMATCH,INTERNAL_CONTRACT_FAILURE}`；`PluginExitKind={NONE,PROCESS_EXIT,NATIVE_STATUS}`；`PluginLogOrigin={KOTLIN,NATIVE,SHELL}`；`PluginLogLevel={DEBUG,INFO,WARN,ERROR}`。 |

允许的stage tuple为：`SUCCESS/NONE/(NONE,0|PROCESS_EXIT,0)`；`FAILED/REJECTED/(NONE,0|NATIVE_STATUS,非0)`；`FAILED/NOT_READY/NONE,0`；`FAILED/IO_FAILURE/(NONE,0|NATIVE_STATUS,非0)`；`FAILED/PROCESS_EXIT_NONZERO/PROCESS_EXIT,1..255`；`FAILED/SIGNALLED/NATIVE_STATUS,非0`；`FAILED/ARTIFACT_IDENTITY_MISMATCH/NATIVE_STATUS,非0`；`CANCELLED/CANCELLED/NONE,0`；`UNKNOWN/(MALFORMED_RESULT|INTERNAL_CONTRACT_FAILURE)/(NONE,0|NATIVE_STATUS,非0)`；`SKIPPED/NOT_STARTED/NONE,0`。null统一`NullPointerException`，grammar/range/tuple统一`IllegalArgumentException`。log message同样single-line printable UTF-8且`<=4096` bytes；Host v1仍以现行printable ASCII＋approved SGR grammar决定是否发布，超出者只增加Host dropped-log事实，不抛回plugin也不改变plugin result，不能把“API允许调用”误写成“所有Unicode已可见”。

`DiagnosticBackendRun.execute`异常的Host映射只有一个adapter：`INVALID_INPUT→primitive FAILED/REJECTED + dispatch SKIPPED`，`NOT_READY→FAILED/NOT_READY + SKIPPED`，`IO_FAILURE→FAILED/IO_FAILURE + SKIPPED`，`CANCELLED→两个CANCELLED`，`CONTRACT_FAILURE`、其它`Throwable`、null／非法DTO→两个`UNKNOWN/INTERNAL_CONTRACT_FAILURE`。`close()`成功产生现行Host `StageResult.SUCCESS`；任意close异常产生Host `StageState.FAILED/ResultReason.CLEANUP_INCOMPLETE`，不要求也不允许把`CLEANUP_INCOMPLETE`加进plugin API reason enum，且不覆盖已经返回的两个stage。entry constructor／`createRun()`异常发生在prepare期并映射为`NOT_READY`、`IO_FAILURE`或`INTERNAL_CONTRACT_FAILURE`；一旦开始class load，本RunHost即标为plugin-tainted且只能走进程终结，不能在同进程重试。

**`.glplugin.zip` format 1.0**就是普通、case-sensitive ZIP／ZIP64；扩展名不隐藏容器类型，用户可用常规ZIP工具解包、修改和重打包。entry顺序、timestamp、压缩级别、ZIP comment、data descriptor及不改变文件内容的safe extra metadata都不属于plugin身份。安全路径检查后允许常见工具产生的directory entry并忽略它们；所有regular-file entry则必须逐一进入manifest。verifier拒绝duplicate regular path、绝对路径、反斜杠、空／`.`／`..`segment、symlink／special file、encryption、非`STORED`／`DEFLATED`method、未声明或缺失文件、矛盾的local／central metadata以及尾随第二archive。ZIP parser负责结构检查并校验每个entry自带CRC；CRC只处理偶发传输／存储损坏，不承担真实性或抗篡改。

格式本身不规定archive字节数、entry数量、manifest字节数、单文件解压大小或总解压大小上限，也不把这些数值写进manifest。实现必须用fixed-size buffer流式读取、hash和提取，不得按ZIP声明大小一次性分配内存；真实I/O、磁盘或内存资源耗尽按`IO_FAILURE`失败并清理workspace。以后external import若需要可配置admission quota，它属于Host资源policy，不得改写format compatibility；B1 builtin受APK签名和构建门约束，不提前设计一套通用quota系统。首批生成物恰为：

~~~text
manifest.json
code/classes.dex
lib/arm64-v8a/libghostlock_diagnose.so
lib/arm64-v8a/libjnidispatch.so
licenses/jna/LICENSE
licenses/jna/AL2.0
licenses/jna/LGPL2.1
~~~

`publisherId/packageId/component id/variant/logicalLoadName`使用`[a-z0-9][a-z0-9._-]{0,95}`；`backendKey`保留现行`BackendDescriptor` grammar `[A-Za-z0-9][A-Za-z0-9._:-]{0,95}`，因此`DIAGNOSE`合法且不被误套小写plugin-id规则；`versionName`必须为1..128 UTF-8 bytes的single-line printable文本；entry class为`<=240` UTF-8 bytes的合法Java binary name，本批固定在`com.ghostlock.plugin.diagnose.*`且exact值仍由component字段给出（不从`packageId`字符串猜Java namespace）；entry path同样`<=240` UTF-8 bytes。

DEX采用Android惯例路径：必须有`code/classes.dex`，可继续有连续编号的`code/classes2.dex`、`code/classes3.dex`……，加载顺序按数字而非字符串排序；不接受`classes1.dex`、编号空洞或其它位置的DEX。producer／archive verifier扫描全部DEX定义，拒绝跨DEX重复class以及`com.ghostlock.plugin.api.v1.*`、`com.ghostlock.app.*`、`java.*`、`javax.*`、`android.*`、`dalvik.*`、`libcore.*`定义；Kotlin runtime与`com.sun.jna.*`是本包刻意携带的private implementation，不在该拒绝集。两个ELF必须为AArch64、64-bit、16 KiB load-aligned且实际`DT_NEEDED`与manifest一致；唯一platform needed allow-set为`libc.so/libdl.so/liblog.so/libm.so`，其它needed SONAME必须指向同archive声明的native library。project DSO必须实际含`SONAME=libghostlock_diagnose.so`；JNA 5.19.1 arm64 `libjnidispatch.so`的已核实ELF没有`DT_SONAME`，其manifest `soname`必须为null且verifier必须观察到实物亦缺省，不得伪造SONAME。本批不把这一窄allow-set扩成全工程通用ELF框架。

source `manifest-source.json`只允许人工填写identity、compatibility、component和native library声明；Gradle producer从最终DEX／ELF／notice bytes派生统一`files[]`哈希表并生成archive内manifest。exact字段为：

| field | 本批值／规则 |
|---|---|
| `formatMajor/formatMinor` | `1/0`；unknown major或高于Host支持的minor拒绝。 |
| `publisherId/packageId/versionCode/versionName` | `com.ghostlock.firstparty`／`com.ghostlock.diagnose`／独立正整数plugin version／展示字符串；同version不同semantic content root是冲突，同root的不同ZIP打包等价。 |
| `variant/minAndroidSdk` | `android-arm64-v8a`／`31`；按RunHost ABI而非kernel target ISA。 |
| `hostApiMajor/minHostApiMinor` | `1/0`；不以App版本猜ABI。 |
| `files[]` | `manifest.json`与directory entry之外每个regular file的一对一`path/sha256`；不区分DEX／native／license，不记录byteCount；`sha256`为64位lowercase hex。未列文件、漏列文件、重复path或hash mismatch均拒绝。 |
| `nativeLibraries[]` | 每个native file的一对一`path/abi/elfClass/elfMachine/soname?/logicalLoadName/neededLibraries`；`path`必须引用一个`files[]`项，nullable `soname`必须与ELF实物有无`DT_SONAME`逐字一致；非native文件没有伪`kind`或空native字段。 |
| `components[]` | exactly一个`kind=BACKEND,id=diagnose.backend`；`backendKey=DIAGNOSE`、`entryContract=diagnostic-v1`、`entryClass=com.ghostlock.plugin.diagnose.DiagnoseEntryPoint`、`selectionPolicy=SELECTABLE`、`abortPolicy=BEFORE_START_ONLY`、`executionDomains=[APP,SHIZUKU_SHELL]`、`runCapability=DIAGNOSTIC_ONLY`。 |

manifest 1.0拒绝unknown field、duplicate JSON key、float/NaN、负数、未列regular file与regular file漏列。`PluginMetadataCodec`只有一条strict decode路径：在typed decode前做同codec内部的duplicate-member syntax检查，再以unknown-key fail-closed的`kotlinx-serialization-json`解码；这不是第二份schema或第二identity authority。source JSON可使用普通whitespace和任意object field order，不再要求source bytes等于canonical encoder输出，也不要求archive entry排序。

decode后的typed manifest按语义规范化：`files`／`nativeLibraries`按path、`components`按id、`neededLibraries`及其它set-like字段逐字排序并拒绝semantic duplicate；再用format-owned canonical encoder只在内存中编码normalized model。`pluginContentSha256 = SHA-256(UTF-8("GhostLockPluginContent/1") || 0x00 || normalizedManifestBytes)`；该值不写回manifest，避免自引用。由于normalized manifest已经包含每个regular file的SHA-256，content root同时绑定package metadata和全部文件内容，但完全不绑定ZIP entry顺序、timestamp、压缩方法／级别或comment。整包ZIP bytes不计算、不存储、不比较SHA-256；同一内容解压后重新压缩仍得到同一`pluginContentSha256`。本批没有`providers`、`requirements`、`exports`、`presentation`、`i18n`或Custom字段；后继真实consumer通过format minor或新component contract增加，不在首批放空数组冒充支持。

**builtin index与catalog**：APK generated asset固定为`assets/ghostlock-plugins/builtin-index.json`和`assets/ghostlock-plugins/objects/<pluginContentSha256>.glplugin.zip`。index schema为`schemaMajor/schemaMinor=1/0`、`records[]`和`defaultBackendKey`；B1 `records`恰好一个，字段只含`publisherId/packageId/versionCode/variant/assetPath/pluginContentSha256`，并指定`defaultBackendKey=DIAGNOSE`。index不得携带archive byteCount／archive SHA-256／manifest byte SHA-256、entry class、component runtime object或文件系统安装path；asset重验后的identity与semantic content root必须逐字段等于index。index与其content root由当前APK签名认证，不是可从外部替换的self-signature；future imported package的publisher signature／trust admission仍留给真实import批。

main process唯一物理catalog位于`noBackupFilesDir/plugin-catalog/`：`objects/sha256/<pluginContentSha256>.glplugin.zip`是0400 immutable object，`catalog.json`由`AtomicFile`提交，`staging/`只含当次未发布临时物。catalog snapshot只保存schema revision、单调catalog revision，以及按`(publisherId,packageId,variant)`唯一排序的active exact records；每条record保存semantic content root、`BUILTIN_SEED(apkVersionCode,indexSha256)` provenance、enabled／removable policy和validated manifest projection，不保存ZIP byte hash／byteCount、class、entry instance、PFD或runtime handle。B1 seeder只产生一个Diagnose record，但catalog不被错误建模成“全局只能装一个包”；这只是已知多package需求的membership容器，不提前加入provider依赖图。

active record只能在object流式写入、fsync、chmod、重开并重新校验ZIP结构／CRC、逐文件SHA-256和semantic content root后提交；object-before-catalog崩溃只留下不被引用的orphan，catalog不得引用未验证／缺失object。若目标content-root path已经存在，installer必须重开并重算内容，而不能只信文件名；验证相同则复用现有object，不因新ZIP的order／timestamp／compression不同而替换物理bytes。每次发布READY前也必须重开所有active object并核content root、manifest和stored projection一致，不能只信`catalog.json`中的投影。

`BackendCatalogProjection`每个READY generation只生成一份immutable `BackendCatalogView`；active records若投影出重复`backendKey`、重复完整component identity=`(publisherId,packageId,componentId)`或非法`BackendDescriptor`，bootstrap整体进入catalog FAILED，不以排序选胜者；不同package复用局部`componentId`本身不冲突。每个key的`ResolvedBackendProjection`同时持有Host `BackendDescriptor`和来自同catalog record的pure `PluginObjectRef(package identity, variant, versionCode, pluginContentSha256)`。UI只取descriptor；`RunSessionCoordinator.start`在接受边界只读一次当前READY view，用selected key取同一projection，并把其exact `PluginObjectRef`与Payload／Finish transaction一起写入`RunInputCutoff`。`AndroidRunInputFreezer`只按该ref向`PluginObjectSource`打开object。这是同catalog snapshot的一次投影，不是第二registry或可变package handle。B1的coordinator在一次application lifetime只发布一个terminal READY generation，且READY后不再改catalog；content-addressed object也只在下一次启动、发布READY之前做orphan／unreferenced GC，因此cutoff到PFD open之间不存在active object被删的窗口。后继import/update若要求READY后热改catalog，必须同批加入cutoff object pin／lease再允许删除旧object，不能只靠ref假设文件仍在。

首次／升级启动顺序固定为：`AppContainer.initialize → BuiltinPluginSeeder.reconcile → PluginCatalogCoordinator.publish READY/FAILED → AppSelection await/migrate/restore → privilege refresh → UI`。同identity低版本不降级；同version同content root的不同ZIP打包视为同一object，同version不同content root为catalog/build conflict；高版本先install再原子切active。catalog失败发布`FAILED(reason)`和空backend projection，初始化仍返回让普通设置／诊断报告UI可进入，Run一律`NOT_READY`。RunHost不读catalog或APK asset，seed也绝不在RunHost执行。

`AppSelection`同批加入`backendSelectionState={FRESH_UNINITIALIZED,SELECTED,NEEDS_USER_CHOICE}`。新文件default为`FRESH_UNINITIALIZED+null`；旧JSON缺state且key非空时一次迁移为`SELECTED`，缺state且key空保持fresh；corruption handler必须写`NEEDS_USER_CHOICE+null`，不能冒充初装。只有fresh且catalog READY时按index的显式default key选择Diagnose；`SELECTED` key缺失／disabled／不兼容转`NEEDS_USER_CHOICE`，不得按列表首项fallback。该顺序使“catalog已提交、selection尚未提交”的崩溃可幂等重放，且不存在selection先指向未安装object的窗口。

UI不再用显示层补一个选择结果。`BackendUiCatalog.selectedEntry()`只在catalog READY且持久化状态确为`SELECTED`时按exact key返回entry；key为null、缺失或不可选均返回null，不退到第一项。`BackendPageUiState`保留nullable `selectedKey/selectedIndex`并携带nullable catalog failure reason：READY但未选择时，Material／Miuix selector显示现有本地化“未选择后端”，列表中没有任何row被标为selected；只有用户点击一个真实selectable row且`SelectBackendUseCase`提交成功，状态才变为`SELECTED`。catalog FAILED时projection为空，路径页和主页现有backend status card都显示本地化失败标题及bounded reason、保持run disabled并引导到路径页；不得插入“catalog failed”或“请选择”synthetic backend row，因为展示行不能成为membership。`INITIALIZING`仍由启动阶段承接，不制造临时membership。

###### 19.5.70 改动清单

本节是`BXP3-B1`唯一source ledger；旧`backend-root-execution-unification-ledger.tsv`中的240行P-BACKEND-X-002旧批次记录继续只作历史，不得作为本批实现清单。本批production文件动作如下；未列文件不因相邻目录被授权修改：

| action | path | exact change／reason |
|---|---|---|
| MODIFY | `settings.gradle.kts` | 只新增`:plugin:api`、`:plugin:core`、`:plugin:packages:diagnose`三个叶project；`:plugin`／`:plugin:packages`只是Gradle path父级，不建空module。 |
| MODIFY | `build.gradle.kts` | pin与current Kotlin/AGP相同版本的KMP及Android-KMP library plugin；把`nativeTestContractsHost`的common／Diagnose sourceInputs改到新authority，保留legacy attack inventory路径；保留Gradle唯一构建入口并让`nativeBuildAndroid`经两App variant消费同一Diagnose package producer，同时更新task说明。 |
| MODIFY | `app/build.gradle.kts` | 加`:plugin:api/:plugin:core`、把同一verified plugin outgoing artifact接入debug/release generated asset；删除JNA coordinate、`:app` `externalNativeBuild`与Diagnose native packaging；普通APK `lib/`不再含project/JNA DSO。 |
| MODIFY | `app/proguard-rules.pro` | 删除Host JNA/Diagnose keep，增加shared API exact keep；不按entry class字符串为Host保留plugin implementation。 |
| ADD | `plugin/api/build.gradle.kts` | Java library、Java 17、无第三方／project runtime dependency。 |
| ADD | `plugin/api/src/main/java/com/ghostlock/plugin/api/v1/PluginApi.java` | revision常量。 |
| ADD | `plugin/api/src/main/java/com/ghostlock/plugin/api/v1/PluginFailureCode.java` | closed failure enum。 |
| ADD | `plugin/api/src/main/java/com/ghostlock/plugin/api/v1/PluginException.java` | bounded checked exception。 |
| ADD | `plugin/api/src/main/java/com/ghostlock/plugin/api/v1/DiagnosticBackendEntryPoint.java` | resource-free entry factory contract。 |
| ADD | `plugin/api/src/main/java/com/ghostlock/plugin/api/v1/DiagnosticBackendRun.java` | blocking execute＋close contract。 |
| ADD | `plugin/api/src/main/java/com/ghostlock/plugin/api/v1/DiagnosticRunContext.java` | identity/process/cancel/log窄Host service。 |
| ADD | `plugin/api/src/main/java/com/ghostlock/plugin/api/v1/DiagnosticRunResult.java` | two-stage result envelope。 |
| ADD | `plugin/api/src/main/java/com/ghostlock/plugin/api/v1/PluginStageResult.java` | exhaustive tuple validation。 |
| ADD | `plugin/api/src/main/java/com/ghostlock/plugin/api/v1/PluginStageState.java` | closed state enum。 |
| ADD | `plugin/api/src/main/java/com/ghostlock/plugin/api/v1/PluginResultReason.java` | closed reason enum。 |
| ADD | `plugin/api/src/main/java/com/ghostlock/plugin/api/v1/PluginExitKind.java` | closed exit enum。 |
| ADD | `plugin/api/src/main/java/com/ghostlock/plugin/api/v1/PluginLogOrigin.java` | closed origin enum。 |
| ADD | `plugin/api/src/main/java/com/ghostlock/plugin/api/v1/PluginLogLevel.java` | closed level enum。 |
| ADD | `plugin/core/build.gradle.kts` | pure KMP commonMain/commonTest＋Android library/JVM targets；只用同版Kotlin serialization plugin与`kotlinx-serialization-json:1.12.0`，无AIDL、native、Compose或App依赖。 |
| ADD | `plugin/core/src/commonMain/kotlin/com/ghostlock/plugin/core/PluginIdentity.kt` | package／component identity与semantic `pluginContentSha256` typed value／grammar；不定义ZIP byte identity。 |
| ADD | `plugin/core/src/commonMain/kotlin/com/ghostlock/plugin/core/PluginPackageManifest.kt` | exact format-1.0 immutable model。 |
| ADD | `plugin/core/src/commonMain/kotlin/com/ghostlock/plugin/core/BuiltinPluginIndex.kt` | APK builtin index exact identity／asset／semantic content root／explicit default纯模型。 |
| ADD | `plugin/core/src/commonMain/kotlin/com/ghostlock/plugin/core/PluginMetadataCodec.kt` | manifest/index/catalog共用的duplicate-aware strict typed JSON decode；允许source whitespace／field order，只对normalized typed semantics内部canonical encode并计算content root。 |
| ADD | `plugin/core/src/commonMain/kotlin/com/ghostlock/plugin/core/PluginCatalogSnapshot.kt` | 以semantic content root引用object的多record catalog／provenance／READY projection纯模型；没有dependency graph。 |
| ADD | `plugin/packages/diagnose/build.gradle.kts` | Android library＋package-owned CMake/JNA；main只`compileOnly(:plugin:api)`，unit test另`testImplementation(:plugin:api)`且producer inputs显式排除test/API；debug/release compilation仍供本module测试，但只有标准`release` compilation的classes/runtime dependency closure与arm64 native进入D8/verified `.glplugin.zip`，发布唯一`glPluginElements` outgoing artifact；允许D8 multidex，禁止两个App build type各产一份同version不同content-root object。 |
| ADD | `plugin/packages/diagnose/src/main/AndroidManifest.xml` | 标准Android library所需的空manifest；不声明component、permission、provider或Host入口。 |
| ADD | `plugin/packages/diagnose/src/main/plugin/manifest-source.json` | 只保存人工identity／compatibility／component／native声明；producer为所有构建产物和notice生成统一`files[]` SHA-256表，不保存ZIP byte hash／size。 |
| DELETE | `app/src/main/assets/third_party/jna/INVENTORY.txt` | inventory不是运行、兼容或真实性合同；不迁移、不打包，也不以AAR／POM／license字节哈希建立JNA专项门禁。 |
| MOVE_REWRITE | `app/src/main/assets/third_party/jna/LICENSE` → `plugin/packages/diagnose/src/main/plugin/licenses/jna/LICENSE` | 复用现有JNA redistribution notice单一source。 |
| MOVE_REWRITE | `app/src/main/assets/third_party/jna/AL2.0` → `plugin/packages/diagnose/src/main/plugin/licenses/jna/AL2.0` | 复用现有JNA Apache-2.0 text单一source。 |
| MOVE_REWRITE | `app/src/main/assets/third_party/jna/LGPL2.1` → `plugin/packages/diagnose/src/main/plugin/licenses/jna/LGPL2.1` | 复用现有JNA LGPL-2.1 text单一source。 |
| ADD | `plugin/packages/diagnose/src/main/kotlin/com/ghostlock/plugin/diagnose/DiagnoseEntryPoint.kt` | shared API entry；不注册同包service。 |
| ADD | `plugin/packages/diagnose/src/main/kotlin/com/ghostlock/plugin/diagnose/DiagnoseRun.kt` | current raw native facts→两个stage的唯一plugin-side映射。 |
| ADD | `plugin/packages/diagnose/src/main/kotlin/com/ghostlock/plugin/diagnose/DiagnoseJnaLibrary.kt` | private JNA mapping。 |
| ADD | `plugin/packages/diagnose/src/main/kotlin/com/ghostlock/plugin/diagnose/DiagnoseNativeBinding.kt` | plugin-owned JNA load/call/pipe/close；设置JNA classloader选项。 |
| ADD | `plugin/packages/diagnose/src/main/kotlin/com/ghostlock/plugin/diagnose/PluginPipeLogCollector.kt` | Android PFD读取与exact Native/Shell log校验，不依赖Host `FdLogConnector`实现类。 |
| MOVE_REWRITE | `app/src/main/cpp/backends/diagnose/CMakeLists.txt` → `plugin/packages/diagnose/src/main/cpp/CMakeLists.txt` | package独占DSO producer并直接编入source-only common object。 |
| MOVE_REWRITE | `app/src/main/cpp/backends/diagnose/diagnose.cpp` → `plugin/packages/diagnose/src/main/cpp/diagnose.cpp` | package-owned native implementation。 |
| MOVE_REWRITE | `app/src/main/cpp/backends/diagnose/api/diagnose_api.h` → `plugin/packages/diagnose/src/main/cpp/api/diagnose_api.h` | 同release private C ABI。 |
| ADD | `plugin/native-source/common/CMakeLists.txt` | 只声明可被具体package／host contract test静态编入的OBJECT source；不产static/shared runtime library。 |
| MOVE_REWRITE | `app/src/main/cpp/common/gl_log.c` → `plugin/native-source/common/common/gl_log.c` | 单一source authority；不再属于Host module。 |
| MOVE_REWRITE | `app/src/main/cpp/common/gl_log.h` → `plugin/native-source/common/common/gl_log.h` | 同上。 |
| MOVE_REWRITE | `app/src/main/cpp/common/run_handoff.c` → `plugin/native-source/common/common/run_handoff.c` | 同上。 |
| MOVE_REWRITE | `app/src/main/cpp/common/run_handoff.h` → `plugin/native-source/common/common/run_handoff.h` | 同上。 |
| MODIFY | `tools/tests/CMakeLists.txt` | host native contract tests读取新的source authority；仍不产生App/Host runtime DSO。 |
| MODIFY | `tools/tests/diagnose_abi_test.cpp` | 把旧`backends/diagnose/api/diagnose_api.h` include改为搬迁后的package header include；测试语义不变。 |
| MODIFY | `app/src/main/cpp/backends/43284glued/CMakeLists.txt` | 仅更新被冻结source-only候选对common source authority的路径；不把43284接回产品图。 |
| MODIFY | `app/src/main/cpp/backends/43284glued/SOURCE-MANIFEST.json` | 同步provenance path，不改变43284批准状态。 |
| DELETE | `app/src/main/cpp/CMakeLists.txt` | App不再有native build入口。 |
| DELETE | `app/src/main/cpp/backends/CMakeLists.txt` | App不再聚合backend native target。 |
| DELETE | `app/src/main/cpp/common/CMakeLists.txt` | common runtime target owner转为package source-only authority；未迁移的AES/HMAC source继续冻结且不进App build。 |
| ADD | `app/src/main/kotlin/com/ghostlock/app/core/plugin/PluginCatalogState.kt` | `INITIALIZING/READY/FAILED` UI/domain状态，不含Android path。 |
| ADD | `app/src/main/kotlin/com/ghostlock/app/data/plugin/AndroidPluginArchiveVerifier.kt` | 普通ZIP结构／entry CRC、逐文件SHA-256、semantic content root、multidex class定义／header与ELF窄验证；main／RunHost复用，流式读取且无格式级固定容量上限。 |
| ADD | `app/src/main/kotlin/com/ghostlock/app/data/plugin/AndroidPluginCatalogStore.kt` | app-private immutable objects＋AtomicFile catalog唯一storage owner。 |
| ADD | `app/src/main/kotlin/com/ghostlock/app/data/plugin/BuiltinPluginSeeder.kt` | index admission、幂等install／upgrade与explicit default key。 |
| ADD | `app/src/main/kotlin/com/ghostlock/app/data/plugin/PluginObjectSource.kt` | main-only exact object open/PFD lease；不暴露path给core或UI。 |
| ADD | `app/src/main/kotlin/com/ghostlock/app/composition/plugin/PluginCatalogCoordinator.kt` | application-scoped bootstrap与单一StateFlow snapshot。 |
| ADD | `app/src/main/kotlin/com/ghostlock/app/composition/backend/BackendCatalogProjection.kt` | 每个catalog READY generation一次生成key→`ResolvedBackendProjection(descriptor, exact PluginObjectRef)`；UI只取descriptor，run cutoff取同一object ref。 |
| ADD | `app/src/main/kotlin/com/ghostlock/app/ui/backend/BackendPresentationRegistry.kt` | 只按已存在catalog key选择Host UI adapter；不能新增membership或runtime。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/ui/backend/BackendUi.kt` | 注释与entry owner改为catalog descriptor＋Host presentation；类型不跨classloader。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/ui/backend/BackendUiCatalog.kt` | 读取唯一catalog snapshot/state；`selectedEntry()`只解析exact persisted selection，删除first-entry fallback并向现有UI投影bounded catalog failure。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/ui/backend/DiagnoseBackendUi.kt` | 保持现有Android文案／Compose内容，只改为presentation adapter。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/ui/screen/module/BackendPageUiState.kt` | `selectedKey/selectedIndex`改为nullable并携带nullable catalog failure；null不映射为index 0。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/ui/viewmodel/PathViewModel.kt` | 从同一catalog state生成READY／FAILED及exact selection页面状态；不得用空字符串或第一项代替未选择。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/ui/screen/module/BackendPage.kt` | 两套skin在null selection时显示“未选择后端”且不标中任何row；FAILED显示reason并禁run，不加入synthetic option。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/ui/component/material/SegmentedList.kt` | 现有`SegmentedDropdownItem`接受nullable selected index；null时不coerce到0、不画check，选择回调和其它caller语义不变。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/ui/viewmodel/HomeViewModel.kt` | 主页status card区分exact selected backend、未选择与catalog FAILED；失败reason可见，现有非ready导航仍只去路径页。 |
| MODIFY | `app/src/main/res/values/strings.xml`; `app/src/main/res/values-zh-rCN/strings.xml` | 复用`backend_run_no_backend`，只补catalog FAILED的本地化format文案；不提前迁移统一i18n。 |
| DELETE | `app/src/main/kotlin/com/ghostlock/app/ui/backend/CustomBackendUi.kt` | 删除未安装却混入backend membership的disabled placeholder；Custom真实批次再由catalog恢复。 |
| DELETE | `app/src/main/kotlin/com/ghostlock/app/composition/backend/BackendDefinition.kt` | 不再存在same-APK outer definition。 |
| DELETE | `app/src/main/kotlin/com/ghostlock/app/composition/backend/BackendRegistry.kt` | installed membership唯一改为catalog。 |
| DELETE | `app/src/main/kotlin/com/ghostlock/app/composition/backend/DiagnoseBackendDefinition.kt` | descriptor来自authenticated manifest。 |
| DELETE | `app/src/main/kotlin/com/ghostlock/app/composition/backend/CustomBackendDefinition.kt` | 不再用假definition预占Custom。 |
| DELETE | `app/src/main/kotlin/com/ghostlock/app/backend/declarations/BackendRuntime.kt` | RunHost runtime由plugin loader构造，不保留平行same-APK factory。 |
| DELETE | `app/src/main/kotlin/com/ghostlock/app/backend/backends/lua/CustomBackendRuntime.kt` | 删除无真实Custom consumer的inert runtime。 |
| DELETE | `app/src/main/kotlin/com/ghostlock/app/backend/backends/diagnose/DiagnoseBackendRuntime.kt` | 被plugin entry取代。 |
| DELETE | `app/src/main/kotlin/com/ghostlock/app/backend/backends/diagnose/DiagnoseFlow.kt` | backend语义迁入plugin `DiagnoseRun`，Host只保留generic adapter。 |
| DELETE | `app/src/main/kotlin/com/ghostlock/app/backend/backends/diagnose/jna/DiagnoseJnaLibrary.kt` | JNA mapping离开Host。 |
| DELETE | `app/src/main/kotlin/com/ghostlock/app/backend/backends/diagnose/jna/DiagnoseNativeBinding.kt` | native owner离开Host。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/backend/README.md` | 重写为catalog、Host presentation与plugin run边界。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/domain/backend/BackendDescriptor.kt` | 注释从same-APK registry改为authenticated catalog projection；字段语义不扩张。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/domain/backend/BackendAvailability.kt` | readiness owner改为catalog/compatibility projection，不允许执行plugin probe。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/domain/settings/AppSelection.kt` | 加selection state并冻结有效组合。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/data/settings/DataStoreAppSelectionRepository.kt` | legacy missing-state migration、corruption→NEEDS_USER_CHOICE、严格验证。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/data/backend/BackendSessionStore.kt` | explicit default只用于fresh；删除列表首项fallback。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/data/backend/BackendSelectionAdapter.kt` | 读取catalog snapshot而非构造期静态registry list。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/core/run/RunInputCutoff.kt` | 和Payload／Finish同时冻结accepted READY view的exact `PluginObjectRef`；不保存path、PFD或catalog service。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/core/run/RunSessionCoordinator.kt` | admission按selected key只读一次READY `BackendCatalogView`，把descriptor／object ref同时带入cutoff；删除constructor-time list与currentSpec首项fallback。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/AppContainer.kt` | 注入catalog store/seeder/coordinator/object source；bootstrap先于selection restore；main不加载plugin code。 |
| MODIFY | `app/src/main/aidl/com/ghostlock/app/run/RunHostRequest.aidl` | 在`backendKey`后按`ParcelFileDescriptor pluginArchiveFd`、`String pluginContentSha256`顺序直接加两个必需字段；不传ZIP byteCount／ZIP byte hash、不建新parcel、不重复manifest identity；其余字段顺序及现有`profileArtifactId`形状不变。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/core/runhost/RunHostRequest.kt` | 加normalized semantic content root与fingerprint；现有profile验证不变。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/domain/run/FrozenRunInput.kt` | 加exact semantic content root／opaque source token；profile仍为现有必需字段。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/data/run/AndroidFrozenRunInputLease.kt` | 同时拥有archive master PFD并恰好一次close/duplicate。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/data/run/AndroidRunInputFreezer.kt` | 从cutoff的exact catalog object打开plugin master PFD并交给同一lease；Diagnose profile fixture流程本批保持。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/data/runhost/wire/RunHostRequestWireCodec.kt` | 映射现有request上的archive PFD＋semantic content root两个字段，并在中途失败时关闭已dup的archive/artifact PFD。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/data/runhost/wire/RunHostBinderEndpoint.kt` | parcel budget/model通过后一次dup archive／ordinary artifact descriptors；所有decode/materialize拒绝窗口交给同一owned holder逆序close。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/data/run/AndroidRunWorkspace.kt` | 在同一per-run root增加`plugin/` staging child并捕获root device/inode/uid；保留retention判据，删除前no-follow复核边界、把需遍历的0500目录暂改0700后逆序unlink，不新增第二workspace root。 |
| ADD | `app/src/main/kotlin/com/ghostlock/app/data/runhost/plugin/PluginApiParentClassLoader.kt` | parent只开放boot/platform与Host-owned shared API。 |
| ADD | `app/src/main/kotlin/com/ghostlock/app/data/runhost/plugin/RunHostPluginLoader.kt` | reverify、0400 extract、DelegateLastClassLoader；以`Class.forName(entry,false,loader)`不初始化地核entry namespace／defining loader／shared API interface，通过后才实例化。 |
| ADD | `app/src/main/kotlin/com/ghostlock/app/data/runhost/plugin/PluginRunResourceOwner.kt` | one-shot run／classloader taint／idempotent close owner。 |
| ADD | `app/src/main/kotlin/com/ghostlock/app/data/runhost/plugin/PluginBackendFlow.kt` | shared API result→现有`BackendRunFacts`的唯一Host adapter。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/data/runhost/host/AndroidRunHostPreparationAdapter.kt` | 删除`resolveRuntime`；按role验证并用loader创建plugin Flow。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/core/runhost/RunHostController.kt` | candidate增加idempotent execution lease；所有prepare loser／pre-start cancel／close窗口先终结lease再删workspace。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/runhost/appuid/AppUidRunHostService.kt` | 删除BackendRegistry；callback death／onUnbind／authorized destroy汇入exitCommitted并物理kill PID。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/runhost/shizukushell/ShizukuShellRunHostService.kt` | 删除BackendRegistry；同一loader；authorized destroy安排进程退出；client death完全沿用`daemon(false)`合同，不另建callback-death/watchdog状态机。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/data/runhost/client/AppRunHostClient.kt` | terminal/rejection后执行close→destroy→unbind→等待binder death；超时quarantine。仅internal constructor可注入默认no-op的“bind后callback前／terminal drain后close前”测试latch，production constructor不可见且不改变业务协议。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/data/runhost/client/ShizukuRunHostClient.kt` | 与App同一teardown判据；不能只依赖unbind flag；复用同样仅internal、默认no-op的两个device-test latch。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/data/runhost/client/AndroidRunHostDispatchAdapter.kt` | request lease关闭plugin archive PFD；teardown未验证不返回Completed。 |
| MODIFY | `app/src/main/kotlin/com/ghostlock/app/runhost/README.md` | 记录catalog不进RunHost、exact object重验、plugin-owned native与one-shot kill。 |
| MODIFY | `docs/development/full-process-uml.md` | 只在implementation切换真实owner同批把已实现链改为catalog→PFD→loader→plugin；设计阶段不先冒充现状。 |
| MODIFY | `docs/development/introduction.md` | implementation验收后把current authority从static definition／App-owned JNA/native更新为catalog／plugin-owned chain，保留原Diagnose历史证据的当时边界。 |
| MODIFY | `openspec/changes/builtin-diagnose-plugin/specs/runhost-wire/spec.md` | 旧`docs/plan/backend-root-execution-runhost-wire.md`已随计划系统迁移退出当前authority；本批只在所属change的现行delta spec同步archive PFD＋semantic content root两个字段、PFD owner／fingerprint和不变的profile/business method边界，不提前同步主规范或新建平行wire文档。 |
| MODIFY | `docs/development/engineering-standards.md` | 实现同批只更新`nativeBuildAndroid`现行命令的owner／产物说明：入口仍唯一，Diagnose native/package改由plugin叶模块产生，不改门禁强度或PASS判据。 |
| MODIFY | `README.md`; `README_ZH.md` | implementation验收同批把same-APK `BackendRegistry`、CUSTOM placeholder及App-owned Diagnose Flow/JNA/DSO的现状说明改为catalog＋builtin package＋plugin-owned native；不先写后继provider/import为已实现。 |

测试动作只服务上述production闭包，不新增独立流程：

| action | path | exact coverage |
|---|---|---|
| ADD | `plugin/api/src/test/java/com/ghostlock/plugin/api/v1/PluginApiSurfaceTest.java` | 逐type/member/descriptor/throws与无额外public surface。 |
| ADD | `plugin/core/src/commonTest/kotlin/com/ghostlock/plugin/core/PluginMetadataCodecTest.kt` | strict JSON unknown／duplicate拒绝；whitespace／field order接受；files/native引用；同语义source产生同root，manifest语义变化产生不同root。 |
| ADD | `plugin/core/src/commonTest/kotlin/com/ghostlock/plugin/core/PluginCatalogSnapshotTest.kt` | active records唯一键、同version同content-root等价／不同root冲突、稳定排序与provenance invariants；不测试未实现的dependency semantics。 |
| MOVE_REWRITE | `app/src/test/kotlin/com/ghostlock/app/backend/backends/diagnose/DiagnoseFlowTest.kt` → `plugin/packages/diagnose/src/test/kotlin/com/ghostlock/plugin/diagnose/DiagnoseRunTest.kt` | 保留全部native/raw→stage/outcome组合；用private binding seam，不依赖Host Flow class。 |
| ADD | `app/src/test/kotlin/com/ghostlock/app/data/plugin/AndroidPluginArchiveVerifierTest.kt` | ordinary ZIP重排／timestamp／压缩变化仍同root；directory entry、CRC／文件内容变化但hash未更新、path、multidex顺序／重复class／forbidden namespace、ELF与content-root negative matrix；不写固定容量边界测试。 |
| ADD | `app/src/test/kotlin/com/ghostlock/app/data/plugin/AndroidPluginCatalogStoreTest.kt` | content-root object-before-catalog、fsync/chmod/reopen重验、AtomicFile commit、orphan、corruption、同root不同ZIP packing复用现有object与只读PFD open边界。 |
| ADD | `app/src/test/kotlin/com/ghostlock/app/data/plugin/BuiltinPluginSeederTest.kt` | clean install、restart、upgrade、same-version same-root repack等价／different-root冲突、crash/orphan、catalog corruption。 |
| MOVE_REWRITE | `app/src/test/kotlin/com/ghostlock/app/composition/backend/BackendDefinitionTest.kt` → `app/src/test/kotlin/com/ghostlock/app/composition/backend/BackendCatalogProjectionTest.kt` | catalog是唯一membership；重复backend key/component fail closed；Host presentation不能创造backend。 |
| MODIFY | `app/src/test/kotlin/com/ghostlock/app/AppContainerInitializationTest.kt` | builtin seed→catalog READY/FAILED→selection restore的唯一初始化顺序；失败不让splash永久等待。 |
| MODIFY | `app/src/test/kotlin/com/ghostlock/app/core/run/RunSessionCoordinatorTest.kt` | start只读一次terminal READY view，selected key无first fallback，cutoff冻结descriptor同源exact object；B1同一application lifetime没有post-READY catalog mutation。 |
| MODIFY | `app/src/test/kotlin/com/ghostlock/app/data/settings/DataStoreAppSelectionRepositoryTest.kt`; `app/src/test/kotlin/com/ghostlock/app/data/settings/KotlinxDataStoreSerializerTest.kt` | FRESH/SELECTED/NEEDS_USER_CHOICE合法组合、legacy missing-state迁移、corruption fail-closed。 |
| MODIFY | `app/src/test/kotlin/com/ghostlock/app/data/backend/BackendSessionStoreAuthorityTest.kt`; `app/src/test/kotlin/com/ghostlock/app/data/backend/BackendSessionStoreLegacySelectionTest.kt`; `app/src/test/kotlin/com/ghostlock/app/ui/backend/BackendUiCatalogTest.kt` | explicit builtin default只用于fresh；missing/disabled/incompatible key进user choice，UI不以presentation或list order创造membership。 |
| ADD | `app/src/test/kotlin/com/ghostlock/app/ui/viewmodel/PathViewModelTest.kt` | READY无选择保持nullable index且只有显式成功选择才变SELECTED；FAILED reason可见且options为空，无synthetic row。 |
| ADD | `app/src/test/kotlin/com/ghostlock/app/ui/viewmodel/HomeViewModelTest.kt` | 主页分别投影exact selection、未选择与catalog FAILED reason；后两者均not-ready且不伪造backend identity。 |
| MODIFY | `app/src/test/kotlin/com/ghostlock/app/data/run/AndroidRunInputFreezerTest.kt` | Diagnose exact package PFD／metadata与现有profile fixture同时冻结；PRODUCT_ROOT profile保持。 |
| MODIFY | `app/src/test/kotlin/com/ghostlock/app/data/run/AndroidFrozenRunInputLeaseTest.kt`; `app/src/androidTest/kotlin/com/ghostlock/app/data/run/AndroidFrozenRunInputLeaseDeviceTest.kt` | plugin master PFD与ordinary artifacts的dup/close恰好一次；device上验read-only regular file与pread offset不变。 |
| ADD | `app/src/androidTest/kotlin/com/ghostlock/app/data/run/AndroidRunWorkspaceTest.kt` | 真文件mode覆盖完整0500 package tree、partial extraction混合目录、normal close删除及retained terminal不chmod／不删除；owner/inode/symlink边界失败保留证据并返回false。 |
| MODIFY | `app/src/test/kotlin/com/ghostlock/app/data/runhost/wire/RunHostRequestWireCodecTest.kt` | plugin PFD＋semantic content root ownership／fingerprint、wire无ZIP size/hash、现有profile与failure close。 |
| MODIFY | `app/src/test/kotlin/com/ghostlock/app/core/runhost/RunHostRequestValidationTest.kt`; `app/src/test/kotlin/com/ghostlock/app/data/runhost/wire/RunHostSharedWireTest.kt`; `app/src/test/kotlin/com/ghostlock/app/data/runhost/wire/RunHostBinderEndpointTest.kt` | semantic content-root grammar／fingerprint、actual Parcel budget、null/writable/`O_PATH`/nonregular PFD、partial-dup close与App/Shizuku共用同一model；断言wire没有ZIP byteCount／byte hash。 |
| MODIFY | `app/src/test/kotlin/com/ghostlock/app/core/runhost/ProductRootScriptPlanMapperTest.kt`; `app/src/test/kotlin/com/ghostlock/app/data/runhost/host/ProductRunPreparerTest.kt` | 为现有product request补exact plugin metadata，并证明plugin handoff不改profile/Payload/Finish/root-script语义。 |
| MODIFY | `app/src/test/kotlin/com/ghostlock/app/data/runhost/host/AndroidRunHostPreparationAdapterTest.kt` | archive→verified manifest→domain/API/ABI→load/createRun顺序、partial extraction／classload／createRun failure cleanup与无BackendRegistry。 |
| MODIFY | `app/src/test/kotlin/com/ghostlock/app/core/runhost/RunHostControllerTest.kt` | execution lease在全部拒绝／cancel／launch／normal／close窗口恰好一次终结，且总在workspace cleanup之前；retained terminal仍不删除。 |
| ADD | `app/src/test/kotlin/com/ghostlock/app/data/runhost/plugin/PluginBackendFlowTest.kt` | DTO/exception/cleanup/outcome与classloader边界。 |
| MODIFY | `app/src/androidTest/kotlin/com/ghostlock/app/backend/backends/diagnose/DiagnoseJnaSmokeTest.kt` | 改为从builtin asset→catalog→RunHost DCL的App／Shizuku smoke，不再直接import binding。 |
| ADD | `app/src/androidTest/kotlin/com/ghostlock/app/data/plugin/BuiltinPluginBootstrapTest.kt` | first install/restart/upgrade/FAILED UI gate与0400 installed object。 |
| MODIFY | `app/src/androidTest/kotlin/com/ghostlock/app/data/runhost/RunHostTestFixtures.kt` | 每个真Binder request同时拥有verified builtin `.glplugin.zip` PFD／content root与现有profile artifact；fixture close不泄漏descriptor。 |
| MODIFY | `app/src/androidTest/kotlin/com/ghostlock/app/data/runhost/AppRunHostSmokeTest.kt`; `app/src/androidTest/kotlin/com/ghostlock/app/data/runhost/ShizukuRunHostSmokeTest.kt` | terminal只有在typed result、log drain及expected binder death都完成后才返回；同时核plugin/JNA DSO来自per-run extracted path。 |
| MODIFY | `app/src/androidTest/kotlin/com/ghostlock/app/data/runhost/RunHostHostDeathTest.kt` | 区分terminal前unexpected death与terminal后expected death；覆盖teardown failure/quarantine，fake binding不能把unbind冒充death。 |
| ADD | `app/src/androidTest/kotlin/com/ghostlock/app/data/runhost/PluginRunHostProcessLifecycleTest.kt` | 连续run、prepare reject、teardown timeout／迟到death均观察PID与quarantine；另含一个专供外部shell gate触发的“bind后自杀main instrumentation process”case，预期instrumentation连接中断而非JUnit PASS。 |
| MODIFY | `app/src/androidTest/kotlin/com/ghostlock/app/ui/backend/BackendUiTest.kt` | membership改从catalog projection读取；删除Custom placeholder/runtime断言，并核双skin下无选择不标中首项、catalog FAILED reason可见。 |
| MODIFY | `app/src/androidTest/kotlin/com/ghostlock/app/architecture/UiBaselineFixtures.kt` | 用catalog-backed Diagnose presentation构造READY、无选择与FAILED fixture，删除对static `BackendRegistry`与Custom placeholder的compile-time依赖；不重写未触及页面。 |
| MODIFY | `tools/kotlin_architecture_d0.py` | 核Host零Diagnose/JNA/native owner、唯一catalog caller、API namespace与module dependency direction。 |
| MODIFY | `tools/tests/test_kotlin_architecture_d0.py` | 对应guard正负fixture。 |
| MODIFY | `app/src/test/resources/architecture/kotlin-file-actions-v1.tsv` | 只追加本批实际Kotlin action；不得复制旧240行插件ledger。 |

###### 19.5.71 数据流／控制流差异

本文把“wire”统一写为**RunHost跨进程请求合同（run wire）**；它是现有Binder/AIDL `IRunHost.prepare(RunHostRequest)`的数据形状，不是网络协议、插件API或第二套registry。

**main admission与run wire**：main在accepted run cutoff时从catalog READY snapshot按selected backend key解析该key唯一对应的active object。这里只给现有`RunHostRequest`直接增加两个字段，不增加`RunPluginPackageParcel`、package graph或新的协议层；路径、catalog revision和完整manifest不跨进程：

| field | exact rule |
|---|---|
| `pluginArchiveFd` | 必需、main从immutable object打开的read-only regular-file PFD；App与Shizuku走同一字段。RunHost以`fstat`核regular file并读取当前实际长度，以`F_GETFL`核`(flags & O_ACCMODE)==O_RDONLY`且`O_PATH`未设置，再用fixed-size buffer从offset 0 `pread`到该实际EOF；不能只因`O_PATH & O_ACCMODE`看似为0就误收，也不信任或改变传入open-file offset。 |
| `pluginContentSha256` | 64位lowercase hex的semantic content root；进入request fingerprint，RunHost解析manifest、校验所有文件后必须重新计算相等。它不是ZIP bytes SHA-256，也不随entry order／timestamp／compression变化。 |

`RunHostRequest`字段顺序固定为现有`runId/generation/backendKey`，随后新增`pluginArchiveFd/pluginContentSha256`，再接现有`profile/payload/finish/artifacts`且后者内部顺序不变。publisher、package、version、variant、component和entry class只以archive内verified manifest为authority，绝不在AIDL重复；现有`backendKey`选择该manifest中的唯一匹配BACKEND component。现有`profileArtifactId`和PROFILE artifact的非null／引用规则不变；`DIAGNOSTIC_ONLY`仍要求Payload／Finish为空，plugin API不可见也不消费Host的过渡profile fixture。结构校验先于workspace／classload；manifest必须恰有一个与`backendKey`匹配、兼容当前role的BACKEND，否则拒绝。request fingerprint只新增semantic content root，不把ZIP size、ZIP byte hash、FD number或Parcel bytes纳入hash。retry使用相同run identity却改变content root或backend key时返回`RUN_MISMATCH`。

wire ownership固定为：`AndroidRunInputFreezer`拥有catalog object master PFD；`toWireRequest`只dup一次交给request；dispatch lease在client返回或未dispatch close时关闭request PFD；RunHost decode接管自己的PFD并在archive copy完成或任意拒绝时关闭。dup/decode中途失败逆序关闭已经取得的plugin／ordinary artifact descriptor。main不把app-private path、entry class、classloader、JNA对象、native address或runtime instance放上wire。

**RunHost prepare与load**顺序固定如下：

~~~mermaid
sequenceDiagram
  participant M as Main process
  participant C as IRunHost client
  participant H as One-shot RunHost
  participant L as Plugin loader
  participant P as Diagnose plugin Kotlin
  participant N as private JNA/DSO

  M->>C: exact catalog object + read-only PFD
  C->>H: setCallback + prepare(RunHostRequest)
  H->>H: request grammar/fingerprint/PFD/process-role basics
  H->>L: copy archive into same per-run workspace
  L->>L: ZIP structure/CRC + manifest/file hashes/content-root verify
  L->>L: verified component API/SDK/ABI/domain/capability checks
  L->>L: create 0400 dex/DSO files, fsync, close, re-open/verify
  L->>L: DelegateLastClassLoader(dex, libDir, filteredApiParent)
  L->>P: public no-arg entry + createRun()
  P->>N: plugin-owned JNA bootstrap/load
  H-->>C: PREPARED
  C->>H: start
  H->>P: execute(context)
  P->>N: gl_diagnose_run
  N-->>P: raw status/uid/pid/nonce/handoff/log facts
  P-->>H: two typed stage facts
  H->>P: close()
  H-->>C: terminal snapshot after cleanup
  C->>H: requestClose + destroy
  H-->>C: binder death / PID exit
~~~

RunHost在复制前只校验request grammar／fingerprint、read-only regular PFD、semantic-root形状与当前process role这些不依赖manifest的事实；execution domain、component capability和entry identity不能在此时猜。随后把archive流式复制进现有per-run workspace的`plugin/00-<content-root-prefix>/archive.glplugin.zip`；该文件同样以`O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC|O_NOFOLLOW`及0400 mode预先创建，经write FD写入／fsync／close后再按0400重开验证，不计算整包ZIP byte hash。loader在ZIP结构／entry CRC、strict semantic manifest、逐文件SHA-256、recomputed content root及DEX/ELF metadata验证完成后，才以verified component检查host API、Android SDK、process ABI、execution domain、run capability和request `backendKey`；全部通过才允许entry classload。archive只作verified container；真正DCL文件为按数字顺序连接的`code/classes*.dex`和`lib/arm64-v8a/*.so`。每个extract目标也用相同flags和0400 mode预先创建，利用已经打开的write FD流式写入并核该文件SHA-256；因而目录中不存在可重新打开为writable的DEX/DSO窗口。`fsync`后close，随后按0400普通文件重开复核；全部文件完成后package目录改0500，再`fsync`该目录及其parent。不从APK、external storage、JNA temp或另一目录fallback。

workspace cleanup只有现有`AndroidRunWorkspace`一个物理owner。workspace创建时捕获root的`st_dev/st_ino/st_uid`；cleanup只能在`PluginRunResourceOwner` execution lease终结且现有terminal retention判据决定“不保留”后开始，retained terminal完全不chmod也不删除。删除使用no-follow walk：先以`lstat/fstat`复核root仍是所捕获的ordinary directory及owner，再对每个将遍历的directory（含完整0500 package tree与partial extraction留下的混合mode目录）复核owner／device／inode／非symlink边界，并仅把该directory暂改owner-only `0700`；0400 archive／DEX／DSO无需改mode。随后按已记录entry的逆序再次no-follow复核并unlink，最后删root。materialize、classload或`createRun`任一失败以及normal close都汇入这一路径；复核、chmod或unlink任何一步失败即返回false并保留剩余证据，不把cleanup写成成功。这样只修复0500目录不可删的现有缺口，不增加第二cleanup journal、retry worker或rollback机制。

classloader固定为公开`DelegateLastClassLoader(dexPath,nativeSearchPath,parent)`，其中`dexPath`按`classes.dex, classes2.dex, ...`数字顺序连接；boot classpath先于plugin，plugin DEX先于filtered parent。`PluginApiParentClassLoader`只把`com.ghostlock.plugin.api.v1.*`委托给Host API loader，其余Host implementation namespace拒绝；plugin DEX自身携带Kotlin runtime和JNA Java classes。verified package门负责在全部DEX中禁止shared API／Host／platform定义及重复class；RunHost再以`Class.forName(entryClass,false,packageLoader)`得到未初始化的entry class，核它的名称在package namespace、defining loader正是package loader且可转为Host-owned `DiagnosticBackendEntryPoint`。误打shared API副本会在load前的DEX扫描拒绝；若仍在type check失败，不运行constructor或static initializer，该已load DEX的进程标为tainted并物理终止。

Host在任何plugin class initialization前完成archive、verified manifest、API、SDK、ABI、domain、capability与entry type检查，并在第一次`loadClass`前标记process `pluginTainted=true`。entry、`createRun`、`execute`、`close`期间临时把TCCL设为package loader并在`finally`恢复。插件的`DiagnoseNativeBinding`在首次触及JNA `Native`前设置`jna.nounpack=true`／`jna.noclasspath=true`；JNA 5.19.1自己的[加载说明](https://raw.githubusercontent.com/java-native-access/jna/5.19.1/src/com/sun/jna/Native.java#L77-L90)表明这两项禁止class-path临时解包，Android仍由JNA／ART的`System.loadLibrary`从调用类所属loader的native search path解析`jnidispatch`，故不能误设会禁掉该必要路径的`jna.nosys=true`。binding再以mapping interface的plugin classloader作为`Library.OPTION_CLASSLOADER`，由plugin代码调用`Native.load("ghostlock_diagnose", …)`；JNA在Android的[target library fallback](https://raw.githubusercontent.com/java-native-access/jna/5.19.1/src/com/sun/jna/NativeLibrary.java#L217-L224)同样会走`System.loadLibrary`。Host不import JNA、不调用`System.load*`／`dlopen`、不读取symbol、不持有mapping／pointer／native handle。App与Shizuku gate必须读取目标RunHost自己的`/proc/self/maps`或等价PID maps，证明`libjnidispatch.so`与`libghostlock_diagnose.so`都来自本run extracted `lib/arm64-v8a/`，且没有APK普通`lib/`、JNA temp或system同名fallback；源码推理本身不记兼容PASS。

App域workspace位于app-private目录；Shizuku域沿现有owner使用shell-owned `/data/local/tmp/<package>-runhost` 0700随机run目录。后者只能防其它UID与意外路径替换，不能声称隔离同为shell UID的并发peer；取得Shizuku／ADB shell的同UID主体本来就具有等价执行权。本批威胁模型是APK-authenticated builtin content、传输／stale／路径替换和Host UI隔离，不是对恶意shell peer做沙箱。即便如此，Shizuku loader仍须用`O_EXCL|O_NOFOLLOW`、0400重开、load前最后一次inode／逐文件SHA-256／content-root核对并在one-shot PID内立即加载；若未来要把不受信任imported code送入shell域，trust与同UID TOCTOU必须在该批重新审查，不能外推B1结论。

`DiagnoseEntryPoint`同包直接构造`DiagnoseRun`和private binding，不向Host registry注册自己再查回。`DiagnoseRun.execute`在native call前经context emit现有`KOTLIN/INFO/diagnose:start`，delivery失败仍不改变结果；随后保持current唯一映射：

1. native status非0：primitive=`FAILED/REJECTED/NATIVE_STATUS/status`，dispatch=`SKIPPED`；其余raw payload不可信。
2. status为0但uid、pid或echo nonce不等于context／本次nonce：primitive=`UNKNOWN/INTERNAL_CONTRACT_FAILURE`，dispatch=`SKIPPED`。
3. identity通过后，native exact log有效→primitive SUCCESS，否则`UNKNOWN/MALFORMED_RESULT`。
4. handoff `Int.MIN_VALUE→SKIPPED`；`0+shell log有效→SUCCESS/PROCESS_EXIT/0`；`0+log无效→UNKNOWN/MALFORMED_RESULT`；`1..255→FAILED/PROCESS_EXIT_NONZERO`；`-10→FAILED/SIGNALLED`；`-6/-7→FAILED/ARTIFACT_IDENTITY_MISMATCH`；`-1..-5/-8/-9/-11→FAILED/IO_FAILURE`；其它→`UNKNOWN/INTERNAL_CONTRACT_FAILURE`。
5. Host在plugin close后才构造cleanup fact并按current优先级派生`DiagnosticOutcome`：cleanup UNKNOWN/CANCELLED→UNKNOWN；cleanup非SUCCESS→FAIL；两个stage缺失或含UNKNOWN/CANCELLED→UNKNOWN；否则任一FAILED→FAIL；仅两者均SUCCESS→PASS。terminal仍为诊断终态且`isProductSuccess=false`。

**execution resource与进程终结**：`PluginRunResourceOwner`在entry classload前建立，成功返回的`DiagnosticBackendRun`立即登记；其`close`为atomic idempotent。`RunHostPreparedCandidate`持有该execution lease，controller在candidate mismatch、concurrent prepare loser、PREPARED未start取消、worker launch失败、worker finally和requestClose fallback中都调用同一close owner；workspace只能在execution lease终结后删除。`createRun()`在返回前抛异常时，plugin必须先关闭本次尚未移交的pipe/thread/fd；JNA／DSO的部分load视为不可回滚process taint，最终仍由OS进程死亡收口。

现有`requestClose()`只把controller转为DESTROYED，不等于进程死亡。本批把两域统一为以下握手：

1. client在terminal或prepare rejection后先完成log drain，再调用`requestClose`。只有`OK/ALREADY_APPLIED`保留原terminal／rejection；其它code改为transport `INTERNAL_CONTRACT_FAILURE`，但仍释放binding让host-side `onUnbind`收口。client在调用`destroy()`前登记并保留binder `DeathRecipient`，调用现有AIDL `destroy()`后释放binding。
2. App service的callback binder death、`onUnbind`、authorized `destroy()`和`onDestroy`汇入process-local atomic `exitCommitted`。一旦提交，所有新business调用只返回`HOST_DIED`；service在当前Binder transaction返回后安排`Process.killProcess(Process.myPid())`，`stopSelf()`只作framework清理而不是死亡证据。
3. Shizuku UserService继续每run unique process suffix并完全沿用[`daemon(false)`的client-death合同](https://github.com/RikkaApps/Shizuku-API/blob/master/api/src/main/java/rikka/shizuku/Shizuku.java#L3063-L3079)；GhostLock不再为同一事件增加service-side callback-death／registration watchdog。正常收尾只由authorized reserved `destroy()`在当前transaction返回后安排`System.exit(0)`，随后client unbind移除service。
4. 正常terminal／rejection收尾时两域都必须观察目标business binder death，不能把`unbindService`／`unbindUserService(remove=true)`本身记为完成。
5. client最多等待5秒expected binder death。death callback必须区分两态：terminal／rejection收口开始前的death仍是unexpected `HOST_DIED`；收口开始后的death只完成独立的expected-death awaiter，不能反向把已经收到的terminal改成`HOST_DIED`。未观察到death时本次最终结果为transport `INTERNAL_CONTRACT_FAILURE`而非Completed，并把该application-scoped client置于quarantine；quarantine只在原binder death后解除，禁止把新run送入可能仍持有旧classloader／DSO的host。
6. main死亡时，App由`onUnbind`／callback binder death收口，Shizuku无论callback是否注册都由`daemon(false)`合同收口；两者都不伪造plugin close、cleanup成功或安全回滚。正常client coroutine取消必须释放binding；Shizuku不在Host内复制第三方已承诺的client-death owner。

因此两次连续App run、prepare rejection后的下一run、以及client-death后的下一run都必须使用不同PID；classloader引用丢弃、GC、`NativeLibrary.dispose`、Service `onDestroy`或`stopSelf()`都不是可接受的卸载证据。

**状态到UI**不经过runtime registry。main唯一`StateFlow<PluginCatalogState>`提供`INITIALIZING`、`READY(snapshot)`或`FAILED(reason)`；`BackendCatalogProjection`只在READY时产descriptor／availability，`BackendPresentationRegistry`再把`DIAGNOSE`关联现有Host UI。run准备、RUNNING、terminal和log仍走现有`RunSessionCoordinator/LogHub`状态，不写回catalog。Diagnose没有provider preparation，故本批不制造虚假的“资源准备状态”；以后provider draft状态进入preparation plane的独立revisioned state，而不是复用catalog或RunHost state。

###### 19.5.72 兼容性与失败恢复

**builtin与imported的区别只存在于admission/policy，进入installed object后的路径必须相同**：

| phase | builtin（本批实现） | imported（后继实现） | 共同不变量 |
|---|---|---|---|
| source | APK generated asset/index | 用户选择的外部FD／以后仓库FD | verifier只读FD，不信任原路径／文件名 |
| trust admission | 当前已安装APK签名认证index及其semantic content root | 未设计；未来须独立冻结publisher signature／user trust | admission通过后都产生exact semantic object identity |
| seed/install | 首次启动／APK升级自动reconcile | 用户明确install/update action | 同一strict archive verifier、immutable object store、AtomicFile catalog |
| enable/remove | B1 builtin强制enabled、不可remove | 未来policy可disable/remove | active exact object仍只来自catalog |
| execution | catalog→PFD→同一App/Shizuku loader | 同左 | RunHost不看provenance，不存在builtin静态registry旁路 |

这张表不宣称imported已实现。未来imported批只能在`AndroidPluginCatalogStore`的verified-object install入口之前增加trust admission和policy UI；若它要求改变manifest、object identity、catalog owner、loader或RunHost合同，必须重新L级审查，不能另建一条import runtime。

**build/toolchain兼容**：[Kotlin官方兼容表](https://kotlinlang.org/docs/multiplatform/multiplatform-compatibility-guide.html)把Kotlin/KMP 2.4.20的“fully supported”上限列为Gradle 9.7.0与AGP 9.3.1。用户于2026-11-21明确把AGP现行唯一版本固定为`9.1.0`；该版本不超过上述AGP上限，但当前Gradle wrapper 9.8.1仍高于表中Gradle上限，因此本批不冒充整个组合“官方完全支持”。旧单变量P0的输入已经被现有source取代，当前不回写或暂删source；恢复执行的第一门是在现有工作树确认Gradle 9.8.1／Kotlin 2.4.20／AGP 9.1.0下Android＋JVM编译、configuration cache与`:app` consumer成立。连续两次同一复验失败即停回debug／重新设计，不擅自改离AGP 9.1.0、改Kotlin/Gradle或用legacy `androidTarget`旁路。Diagnose native叶使用普通Android library，符合Android官方对KMP plugin不承载`externalNativeBuild`的边界；`:app` application/AIDL继续Android-specific，P-UI后继再迁`:app:shared`。

**catalog／启动恢复**：

- install先object后catalog；任一失败保持旧active records，不修改selection。重启可重验同content-root orphan后复用或清理，不能仅因文件名存在就信任。B1只在bootstrap发布READY之前清理无record引用的旧object，READY后不做object GC；这同时关闭run cutoff尚未打开PFD时的删除竞态。
- catalog JSON损坏、任一active object缺失、index与asset不符、same-version different-content-root、manifest semantic root与index不符均fail closed为catalog FAILED；同root但ZIP packing不同等价，不覆盖损坏catalog伪装fresh install。
- APK升级带更高plugin version时新object验证完成才原子切换record；旧object只在下一次bootstrap、确认无catalog引用且尚未开放run时删除。源码恢复只依赖Git／Time Machine，不生成rollback脚本、备份树或演练副本。
- APK降级遇到catalog中更高version时不自动降级；若旧Host API不能兼容则READY中该backend为NOT_READY并提示，而不是偷偷装旧asset覆盖。
- selection migration只对“旧文件缺state”这一已知形状生效；其它unknown／malformed继续走corruption→NEEDS_USER_CHOICE。

**稳定失败映射**：

| failure | owner | outward result／cleanup |
|---|---|---|
| index/asset/catalog grammar、file hash／content root、version conflict | main bootstrap | catalog FAILED；无backend admission，不启动RunHost |
| wire grammar、fingerprint或PFD／semantic-root basics mismatch | RunHost structural validator | `INVALID_REQUEST`，关闭PFD，未读取manifest、未classload |
| ZIP structure／CRC、strict manifest、逐文件SHA-256／content root、multidex或ELF metadata mismatch | archive verifier | `ARTIFACT_INVALID`，按唯一workspace cleanup删除partial／0500 tree，未classload |
| verified manifest没有exact matching BACKEND component，或request backendKey与其不一致 | loader preflight | malformed component set为`ARTIFACT_INVALID`；valid package与request不匹配为`INVALID_REQUEST`；均未classload |
| entry namespace/defining loader/shared API type mismatch | plugin loader | `ARTIFACT_INVALID`；未初始化entry，但已load DEX的RunHost标为tainted并必须kill |
| verified component的host API minor、Android SDK、process ABI或capability不兼容 | loader preflight | `NOT_READY`，未classload |
| verified component的execution domain与当前process role不匹配 | loader preflight | `EXECUTION_DOMAIN_MISMATCH`，未classload |
| copy/extract/fsync/classloader native search或plugin-declared I/O失败 | materializer | `IO_FAILURE`；若已classload则process-tainted并必须kill |
| entry/interface/DTO/undeclared Throwable合同违例 | materializer/Flow adapter | prepare `INTERNAL_CONTRACT_FAILURE`或运行中两个UNKNOWN；cleanup照实际close |
| terminal前process death | existing transport owner | `HOST_DIED`；不伪造terminal、cleanup或副作用回滚 |
| terminal已到但expected binder death超时 | client teardown owner | 最终transport `INTERNAL_CONTRACT_FAILURE`＋quarantine；不返回Completed |

本批Diagnose没有kernel write／held-state，因此正常close只治理pipe/JNA Java owner；它不能给future attack backend提供“kill process即可安全回滚”的先例。真实攻击backend仍须在自己的vertical审查waiter/race/payload/native handle与kernel side effect终结顺序。



<!-- source: f882e670:docs/plan/backend-root-execution-unification-plan.md:L4864-L4878 -->
###### 19.5.74 明确保留

- `P-BACKEND-X-001/批次02`断开的43499／43284／98260 production graph保持断开；本批只为43284 source manifest更新common源码路径，不编译、注册、打包或宣称支持它。
- 上述断图backend及caller-zero AES/HMAC的legacy物理目录本批不搬；因此“Host零native owner”只在`:app` active build/runtime与APK artifact上关闭，repository目录全迁移明确留给对应真实plugin批，不能把未迁源码误报为已插件化。
- current `RunHostController`仍是唯一state/candidate/worker/callback/quiescence owner；新增execution lease并入现有in-flight epoch，不建立平行controller、journal或另一个RunHost service。现有AIDL business method set不变。
- `IRunHost`的App／Shizuku两domain、typed snapshot、log source、profile、Payload／Finish、root script/result、workspace retention与host-death语义保留；只有plugin archive handoff和物理destroy闭包变化。
- `run_handoff`仍由Diagnose private DSO同步调用并等待child；Host不重新拥有shell `Process`。Native/Shell exact log继续独立验证，log文字不是typed result authority。
- current `BackendDescriptor`字段、selection key=`DIAGNOSE`、`DIAGNOSTIC_ONLY`、`BEFORE_START_ONLY`和`APP_OR_SHIZUKU`语义保持；descriptor来源从static Kotlin definition切为authenticated manifest projection。
- 现有Host `DiagnoseBackendUi`、Android XML文案和Material/Miuix renderer暂时保留；它们只是presentation adapter。P-UI-CORE-001以后再把App/plugin text统一迁入KMP `I18nRegistry`，不能用本批未迁i18n阻断plugin runtime，也不能把XML误写为终态。
- `plugin/core`本批只放已有consumer的manifest/catalog纯模型；Document、provider resolver、presentation、i18n和Custom model等到对应consumer时增量加入，禁止以目录名为由先造空API。
- builtin不可disable/remove只是B1产品policy，不进入manifest兼容规则；future imported/builtin policy仍由catalog层拥有，RunHost loader不分来源。
- 只支持`android-arm64-v8a`是本批真实target；thin archive命名与variant字段保留未来扩展，但未构建ABI不标supported。
- project root／module build output继续只在各自`build/`；archive payload、DEX、`.so`与generated APK asset不进入源码目录。预建KO与用户KO继续不在本批处理。
- 不新增rollback script、备份树、第二ledger、第二catalog、第二locale resolver、通用service locator、SAT solver、OCI/TUF或远程repository。源码恢复沿用Git／Time Machine；runtime atomic install只保护active records与其objects的一致性。



<a id="acceptance-gl-dek"></a>

### 验收定义与原边界：gl-dek

<!-- migrated-source: acceptance_criteria gl-dek -->
2026-11-20生产批准保留；scope仅§19.5.70的148行。2026-11-21用户固定AGP `9.1.0`后，旧AGP `9.4.1` P0只保留为历史证据；当前树须按下表在`9.1.0`上复验，不回退source重演空模块实验。实际状态与依赖只读Beads。不接provider/import/Custom/真实backend，不用旧240行。

<!-- source: f882e670:docs/plan/backend-root-execution-unification-plan.md:L4839-L4863 -->
<a id="b1-validation-matrix"></a>

###### 19.5.73 验证矩阵

设计审查只判断下列门能否证明本批目标，不把预期写成PASS。获得production批准后才按顺序执行；同一门连续失败两次停止implementation并转debug。所有Gradle writer串行，结果读取fresh structured report；定点用例不能冒充全量：

| gate | exact method | 必须观察的结果 | 运行证据入口 |
|---|---|---|---|
| B1-P0 / AGP 9.1.0 current-tree toolchain复验 | 不回退现有source；固定AGP `9.1.0`，先以`:plugin:core:tasks --all`保存Android单variant compilation实际task identity，再串行执行`./gradlew :plugin:api:test :plugin:core:assemble :plugin:core:jvmTest :app:compileDebugKotlin --configuration-cache --console=plain`并原命令复跑；`assemble`必须消费Android与JVM产物而非只构建JVM | Gradle 9.8.1＋Kotlin 2.4.20＋AGP 9.1.0的当前source组合在本工程实际成立；Android/JVM都真实编译，第二次复用configuration cache；无legacy `androidTarget`、只跑JVM、擅自改离AGP 9.1.0或其它toolchain旁路；结果不冒充旧空模块单变量P0重演 | Beads `gl-dek`（旧9.4.1结果保留为历史；当前9.1.0结果另行记录） |
| API／core／package unit | `./gradlew :plugin:api:test :plugin:core:jvmTest :plugin:packages:diagnose:testDebugUnitTest :plugin:packages:diagnose:testReleaseUnitTest --console=plain` | API inventory逐member一致；manifest/catalog/content-root、JSON编辑自由度、multidex全矩阵；Diagnose raw→stage、异常与close组合全通过，0 failure/error/skip/unknown | Beads `gl-3ki`（原运行记录保留于gl-dek notes） |
| App full unit | `./gradlew :app:verifyDebugUnitTestXml :app:verifyReleaseUnitTestXml --console=plain` | catalog seed/crash/selection、wire PFD owner、loader、Flow adapter、controller lease/client teardown及现有全部unit两variant全量通过 | Beads `gl-3ki`（原运行记录保留于gl-dek notes） |
| source architecture | `python3 tools/tests/test_kotlin_architecture_d0.py`后执行`python3 tools/kotlin_architecture_d0.py verify --repo . --fixtures app/src/test/resources/architecture --section all` | `:app` active build/runtime的Diagnose/JNA/native owner=0，且legacy断图native source只允许本节具名旧目录；static BackendRegistry/runtime=0；App→Diagnose implementation edge=0；API→Kotlin/Android/JNA=0；plugin→Host implementation=0；唯一catalog writer／presentation projection／loader成立；`UNCLASSIFIED=0` | Beads `gl-3ki`（原运行记录保留于gl-dek notes） |
| native host contracts | `./gradlew nativeTestHost --console=plain` | 搬迁后的`gl_log/run_handoff/diagnose_api`当前host contract全量通过；source-only 43284没有进入产品build | Beads `gl-3ki`（原运行记录保留于gl-dek notes） |
| Android build | `./gradlew nativeBuildAndroid --console=plain`，其现行说明同批改为“verified plugin native/package + debug/release App总入口” | leaf package只产一份Diagnose DSO与`.glplugin.zip`，两个App variant消费同一package artifact与content root；任务不再意味着`:app`持有native/JNA distribution | Beads `gl-3ki`（原运行记录保留于gl-dek notes） |
| plugin ZIP package | `:plugin:packages:diagnose:verifyGlPluginZip` | ZIP结构／CRC、strict semantic manifest、全部regular files通用hash、multidex与ELF通过；JNA许可证只作为随包再分发文本，不设inventory或AAR／POM／license字节专项身份门。把同一内容用不同entry order／timestamp／compression重打包后ZIP bytes可不同但content root必须相同；任一文件内容或manifest语义变化必须改变root或因hash不符被拒绝；debug/release APK index都指向该content root，不要求reproducible ZIP bytes | Beads `gl-3ki`（原运行记录保留于gl-dek notes） |
| APK boundary | `:app:verifyDebugBuiltinPluginBoundary`与`:app:verifyReleaseBuiltinPluginBoundary` | APK asset/index各恰一个匹配archive；App DEX无`com.ghostlock.plugin.diagnose`／JNA，普通APK `lib/`无`libghostlock_diagnose.so`／`libjnidispatch.so`；plugin DEX无API／Host classes；App classpath无Diagnose project runtime edge | Beads `gl-3ki`（原运行记录保留于gl-dek notes） |
| assemble／AIDL | `./gradlew :app:assembleDebug :app:assembleRelease :app:assembleDebugAndroidTest --console=plain`并核generated AIDL schema | 两variant和test APK成功；AIDL只新增archive PFD＋semantic content root两个字段且没有ZIP byteCount／byte hash，profile形状与现有business method set都不变 | Beads `gl-3ki`（原运行记录保留于gl-dek notes） |
| Android bootstrap | 6.6 arm64 AVD fresh install、same-APK restart、模拟higher plugin version upgrade与corrupt/index mismatch negative instrumentation | fresh显式选择DIAGNOSE；restart不重复安装；upgrade object-before-catalog原子；无选择不高亮第一项；FAILED reason在路径页和主页可见且无synthetic membership；run NOT_READY；installed/archive/extracted DCL文件mode符合0400/0500，normal／partial failure cleanup可删除完整0500 tree而retained terminal不改mode | Beads `gl-bvj`（原运行记录保留于gl-dek notes） |
| App／Shizuku DCL smoke | 同一current-tree app/test APK分别执行pluginized Diagnose App和Shizuku exact class，并采集main及目标RunHost maps | 两域均从catalog asset→PFD→RunHost loader→plugin JNA/DSO，status/uid/pid/nonce/双log/handoff/cleanup与diagnostic PASS保持；两个DSO只映射本run extracted path，main PID从未映射plugin/JNA DSO | Beads `gl-bvj`（原运行记录保留于gl-dek notes） |
| process death | 普通instrumentation覆盖连续两次App/Shizuku run、prepare archive reject→下一run、teardown timeout→quarantine→迟到death解除；另用专用exact class分别在internal latch的“bind后callback前”和“terminal drain后requestClose前”调用`Process.killProcess(mainPid)`。这两次`am instrument`连接中断是预期trigger，外部shell随后轮询已记录的RunHost PID消失并再启动下一run | normal/reject每次都观察旧binder death、旧PID消失且下一run PID不同；两个client-death trigger不能被误计JUnit PASS，必须各自由后续PID absence＋fresh run共同判定；death timeout产生transport failure＋quarantine而非Completed；无cached old DSO/classloader复用 | Beads `gl-bvj`（原运行记录保留于gl-dek notes） |
| physical arm64 device | 同一具名ARM64真机、同一current-tree app/test APK分别执行App域和Shizuku域正常Diagnose；各域记录APK SHA-256、plugin semantic content root、manifest逐文件DEX／ELF SHA-256、ABI/page size、PID序列、maps、typed result及destroy后的business binder death／PID absence，不记录或要求整包ZIP SHA-256 | 两域都必须各自证明catalog→PFD→DCL→plugin-owned JNA/private DSO→close/destroy→physical death；两个DSO只来自本run extracted path且下一run PID不同。Shizuku不可用时该域与B1-V3／B1总关闭保持`NOT_RUN`／open，不以App域代替，也不新增callback/watchdog；只证明Diagnose，不证明root product | Beads `gl-xme`（原运行记录保留于gl-dek notes） |
| deferred matrix | external import trust、provider/profile独立更新、Custom/raw memory、ARMv7/x86/x86_64、Desktop、43499/43284/98260 | 本批不得执行假fixture并宣称支持；各自在后继真实consumer批补门 | Beads 后续任务：`gl-dt8/gl-9lz/gl-tz3/gl-75t/gl-m0f/gl-2u2/gl-2ot`；仅在对应真实consumer批执行，未细化项保持后续范围 |

`:app`本批目标态“零native owner”严格指active build/runtime：没有App-owned C/C++ task/input、JNA/JNI dependency、project DSO或普通APK native entry；root `nativeTestHost`和package leaf CMake仍可存在，不因此把Host重新定义为native owner。当前已断图的43499／43284／98260 source仍物理位于legacy `app/src/main/cpp/backends/`，以及caller-zero AES/HMAC source仍位于legacy common目录；B1不编译它们，也不把大规模物理搬迁偷渡进Diagnose vertical，但这项目录债必须由各真实backend plugin批迁走，故本批不得宣称“repository内所有native source都已离开app目录”。JNA第三方bytecode内部调用`System.loadLibrary`不是Host source调用，但必须只在plugin classloader／nativeSearchPath内成功；缺库、错ABI和APK fallback负例都要失败。

设备证据必须同时记录source checkout、APK SHA-256、plugin semantic content root、manifest逐文件DEX／ELF SHA-256和PID；整包ZIP SHA-256不是证据项。“assemble成功”“service stop”“unbind返回”或“terminal收到”均不能替代物理process death。App与Shizuku是B1已经承诺的两个execution domain，V3必须在同一具名ARM64真机逐域关闭；若设备上Shizuku尚不可用，则Shizuku行、B1-V3和B1总关闭继续`NOT_RUN`／open，除非用户以后明确把production scope改成App-only并重新处理该实质范围变更。这里仍只依赖Shizuku `daemon(false)`合同，不增加callback-death owner或watchdog。因本批不改真实攻击primitive，不要求root exploit成功判据，但DCL／plugin-owned native与process lifecycle本身是设备依赖行为，必须有上述双域真机证据后才能关闭B1-V3。



批准/审核/实际结果：Beads `gl-dek` notes；本段不维护实时勾选或新PASS。

<a id="record-gl-3ki"></a>

## P-BACKEND-X-002：BXP3-B1 普通与包边界验证

<!-- migrated-source: 043fdf6a Beads gl-3ki description -->
本节是迁入OpenSpec的现行设计/验收正文；Beads只保存执行状态、原批准和实际证据。历史来源仅作追溯。
NOT_RUN：API/core/package/App unit、architecture、native、ZIP/APK边界和assemble，按[本批验证矩阵](#b1-validation-matrix)逐项真实执行；构建不冒充合同验收。
原始来源（已归档/承接）：docs/archive/20261121-1301-backend-root-execution-unification-plan.md#p-backend-x-002-decision-03-batch-01

<!-- migrated-source: design gl-3ki -->
共享设计按下列OpenSpec链接读取；原任务范围/批准/运行状态由其Beads记录定位，不展开全部依赖。
- OpenSpec [gl-dek](design.md#record-gl-dek)；对应Beads只查执行/证据。

<a id="acceptance-gl-3ki"></a>

### 验收定义与原边界：gl-3ki

<!-- migrated-source: acceptance_criteria gl-3ki -->
NOT_RUN：API/core/package/App unit、architecture、native、ZIP/APK边界和assemble，按[本批验证矩阵](#b1-validation-matrix)逐项真实执行；构建不冒充合同验收。

批准/审核/实际结果：Beads `gl-3ki` notes；本段不维护实时勾选或新PASS。

<a id="record-gl-bvj"></a>

## P-BACKEND-X-002：BXP3-B1 AVD 双域与进程死亡验证

<!-- migrated-source: 043fdf6a Beads gl-bvj description -->
本节是迁入OpenSpec的现行设计/验收正文；Beads只保存执行状态、原批准和实际证据。历史来源仅作追溯。
NOT_RUN：AVD bootstrap、App/Shizuku DCL与process-death matrix；原前提/判据不变，不外推真机。
原始来源（已归档/承接）：docs/archive/20261121-1301-backend-root-execution-unification-plan.md#p-backend-x-002-decision-03-batch-01

<!-- migrated-source: design gl-bvj -->
共享OpenSpec设计位置（仅按当前任务疑点展开，不复读所有依赖）：
- OpenSpec [gl-dek](design.md#record-gl-dek)；对应Beads只查执行/证据。

<a id="acceptance-gl-bvj"></a>

### 验收定义与原边界：gl-bvj

<!-- migrated-source: acceptance_criteria gl-bvj -->
NOT_RUN：AVD bootstrap、App/Shizuku DCL与process-death matrix；原前提/判据不变，不外推真机。

批准/审核/实际结果：Beads `gl-bvj` notes；本段不维护实时勾选或新PASS。

<a id="record-gl-xme"></a>

## P-BACKEND-X-002：BXP3-B1 ARM64 真机验证

<!-- migrated-source: 043fdf6a Beads gl-xme description -->
本节是迁入OpenSpec的现行设计/验收正文；Beads只保存执行状态、原批准和实际证据。历史来源仅作追溯。
NOT_RUN：具名ARM64真机DCL/native/process-lifecycle，保留原设备和门禁判据；不包含root产品成功。
原始来源（已归档/承接）：docs/archive/20261121-1301-backend-root-execution-unification-plan.md#p-backend-x-002-decision-03-batch-01

<!-- migrated-source: design gl-xme -->
共享OpenSpec设计位置（仅按当前任务疑点展开，不复读所有依赖）：
- OpenSpec [gl-dek](design.md#record-gl-dek)；对应Beads只查执行/证据。

<a id="acceptance-gl-xme"></a>

### 验收定义与原边界：gl-xme

<!-- migrated-source: acceptance_criteria gl-xme -->
NOT_RUN：具名ARM64真机DCL/native/process-lifecycle，保留原设备和门禁判据；不包含root产品成功。

批准/审核/实际结果：Beads `gl-xme` notes；本段不维护实时勾选或新PASS。
