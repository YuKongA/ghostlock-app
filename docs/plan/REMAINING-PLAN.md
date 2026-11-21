# 剩余计划入口与迁移前快照

当前剩余项统一从 [MASTER-PLAN 任务入口](MASTER-PLAN.md#active-task-routing) 进入 OpenSpec/Beads；运行 `npm run --silent bd -- list`。本页不再维护第二份进度；下文只是2026-11-21切换前的证据/沿革快照，不是待执行清单，旧未勾选项须按当前有效任务核对。

<details>
<summary>迁移前快照（只读，不更新 checkbox）</summary>

> **代号范围**：当前项使用[主计划语义代号](backend-root-execution-unification-plan.md#plan-semantic-identifiers)；历史批准、纠错记录和 R5-S1 等来源子号保留兼容，不改变本页产品状态。

> 本文件只投影尚未完成的产品项、gate与已裁定的断开状态；完整语义、逐文件scope与判据以[backend-root-execution-unification-plan.md](backend-root-execution-unification-plan.md)为唯一authority。当前没有自动排队的真实backend child。

## 已完成：[P-UI-ICON-001/批次01：全项目语义图标目录](backend-root-execution-unification-plan.md#p-ui-icon-001-batch-01)

> 状态：source与自动局部门已完成；2026-11-08开发者完成Material3/Miuix目视检查并确认无显示错误，明确关闭IC-V1。随后开发者裁定`verifyJnaDistribution`为不必要检查并从current Gradle task graph删除；历史checksum失败保留为发生过的记录，不再是current gate。

- [x] IC-I1：新增唯一人工 `ui/icon/Icons.kt`，提供Material/Miuix pair、UiMode resolver与单侧问号fallback。
- [x] IC-I2：迁移ledger内全部current direct upstream icon callsite，architecture guard要求目录外命中0。
- [x] IC-V1：自动source/compile门通过，开发者目视Material3/Miuix页面无显示错误并接受当前图标UI。

## 后续需求：[P-UI-ICON-002：Material/Miuix 全量图标目录](backend-root-execution-unification-plan.md#p-ui-icon-002)

> 状态：开发者需求已登记；全量生成、命名、配对与体积合同尚待单独设计，不夹入P-UI-ICON-001。

- [ ] IC2-D1：冻结当前依赖全部style/weight的生成输入、并集配对、fallback、升级重生成与APK/编译成本判据，再提交critic和用户批准。

## 已完成：[P-BACKEND-CORE-001/批次01：BackendDefinition、BackendDescriptor 与具名BackendUi边界](backend-root-execution-unification-plan.md#p-backend-core-001-batch-01)

> 状态：BD-I1/I2 source、architecture、unit、host native、6.6 AVD两皮肤Backend UI均通过；删除不必要的JNA distribution task后，clean与只构建debug/release的`nativeBuildAndroid`均通过，BD-V1关闭。

- [x] BD-I1：完成 `BackendSpec→BackendDescriptor`，把 `BackendPlugin/BackendRegistry` 移到outer `composition/backend`，新增backend-owned runtime entry与UI-owned `BackendUi/BackendUiCatalog`；删除旧data/domain catalog链，同一private definition list只派生inner投影且保持双向禁边。
- [x] BD-I2：删除 `BackendCarrierArtifact`、presentation facts、generic configuration四变体/adapter/renderer/cards；接通Diagnose具名UI与Custom disabled占位。
- **BD-I3（DEFERRED）**：等具体backend出现真实配置consumer时重新设计并批准；本批不新增repository/schema/shared reference/JCS freeze或Diagnose配置开关。
- [x] BD-V1：普通门、architecture、旧generic配置残留0、配置延后边界与两皮肤Backend页证据通过；不接回真攻击backend。

## 已完成：[P-LOG-X-001/批次03：LogConnector 与全局顺序](backend-root-execution-unification-plan.md#p-log-x-001-batch-03)

> 状态：2026-11-08开发者批准的46行source/caller闭包已实施；批次01-02的双流、普通Info报告行与独立sink结果继续保留。debug/release各120项unit、architecture、host 31项、双variant build、>10 MiB MediaStore/两皮肤UI 7项以及App/Shizuku exact connector路径均通过，LC-I1/I2/V1关闭。

- [x] LC-I1：加入返回 `Drained/Incomplete` 的最小 `LogConnector` 与Kotlin/RunHost/FD实现；LogHub/forwarder支持动态attach/detach并删除direct callback ingress。
- [x] LC-I2：落实各source的producer/write/read owner、stop→EOF/terminal marker→collect drain→reader close、peer-death/incomplete语义，以及source FIFO、transport-private sequence、raw acceptance全局commit和timestamp仅元数据。
- [x] LC-V1：普通门、>10 MiB、App/Shizuku并发source、detach前缀完整性与UI/private/MediaStore同commit order证据通过。

## 当前插入批：[P-SETTINGS-IO-001/批次01：全项目 typed DataStore 持久化收口](backend-root-execution-unification-plan.md#p-settings-io-001)

> 状态：五个已有真实consumer的完整data class typed DataStore、主进程初始化、accepted transaction、Payload-only preview exact handle与run cutoff均已实施；旧SharedPreferences adapter和Payload/Finish v3/JCS内部authority已清零。JVM/architecture/普通§8门通过；当前无获准AVD，Android instrumentation runtime保持`NOT_RUN`。Diagnose没有在本批预建空store，未来由P-DIAG在真实consumer同批拥有其具名typed DataStore与snapshot链。

- [x] DS-R1：完成UI/Operational/执行器/backend选择/Diagnose设置/Payload/Finish、导入资产、profile、custom包、workspace、日志、诊断包、build asset与transient state分类。
- [x] DS-G0：用户修订AGENTS §4，限定GLK profile只服务需要系统内核偏移信息的backend；Diagnose/43284/应用设置使用具名typed authority，SETTINGS-REQ-CONFIG-01关闭且不构成越权技术债。
- [x] DS-D1：五aggregate、不迁移、损坏/backup/UI-process/幂等首帧、accepted transaction与cutoff snapshot及93行scope已冻结；同岗critic阻断=0。
- [x] DS-I1：五个typed stores/suspend repositories已实施，被取代的SharedPreferences adapter、内部手写codec/document authority与无职责空壳层已清零。
- [x] DS-I2：启动加载、自动保存→Payload-only preview exact handle、Operational/Payload/Finish Flow cutoff、执行器/backend选择已接通；Diagnose store/snapshot/跨进程承载全部留给真实consumer所在的P-DIAG批。本批未扩入script/LKM resolver、profile editor、custom package或log retention。
- [ ] DS-V1：debug/release JVM exact各130项覆盖round-trip/reopen、默认/损坏、幂等初始化、首帧输入、保存顺序、owner隔离和旧authority归零；Android test Kotlin源码编译通过，但instrumentation runtime因当前无获准AVD而`NOT_RUN`。
- [x] DS-V2：`nativeTestHost`=`TOTAL=31 FAILED=0`；`nativeBuildAndroid`在旧输出重复class后按既定规则`clean`并重跑通过debug/release双variant。该证据不外推为root或真攻击backend成功。

## 已完成批次：[P-RUN-X-001/批次02：Kotlin 全树 Clean Architecture 与 RunHost owner 收口](backend-root-execution-unification-plan.md#p-run-x-001-batch-02)

> 状态：exact RunHost owner/package 收口已完成，旧package/类名、双owner和caller-zero carrier已清零；source critic确认源码与结构PASS、阻断=0，最终architecture/unit/native/build/AIDL/6.6 AVD App+Shizuku门已关闭。`P-DIAG-NATIVE-001/批次02` 的产品需求保留，但逐文件 ledger 要等后续获批Kotlin owner批次稳定后重生。

- [x] KT-D0：扫描全部 production Kotlin 文件，确认 `backend/transport` 混合 pure contract、wire、client、framework service、host preparation、通用 FD reader 与 caller-zero carrier；确认 `backend.transport ↔ data.run`、`core.run → data.run/Android` 及 UI 双向包边。
- [x] KT-D1：核对现行统一计划、已归档 D7 与 `64a56cd6`/`f878ba08` 历史，确认在 `P-RUN-X-001` 内修订而不是恢复第二份并行 Clean Architecture 计划。
- [x] KT-D2：design-critic 首轮与定点复审已关闭 KT-CA-01..08；74 行 exact ledger 路径唯一、直接 caller 闭包完整，总判 PASS、阻断=0、重要=0。
- [x] KT-D3：用户明确批准 `P-RUN-X-001/批次02`；本授权不扩展到 core/data 拆分、Host 去重、backend package 或 UI feature 后续批。
- [x] KT-I1：仅实施获批的 RunHost owner/package 收口；74行exact ledger闭包失败数为0，core/data、Host 去重、backend package 与 UI feature 收口没有偷做。
- [x] KT-V1：architecture=`PASS/RUNHOST_OWNER_BOUNDARY=0/FILE_ACTION_ROWS=562`，debug/release unit各`128`且四桶全0，androidTest compile、native host=`31/31`、双variant build与AIDL zero-diff通过。
- [x] KT-V2：6.6 AVD `emulator-5554` 上 App/Shizuku exact smoke各`OK (1 test)`；不外推为lifecycle全矩阵、真实设备、carrier、root或产品成功。

## 已完成批次：[P-RUN-CORE-001/批次02 core/data 依赖方向修复](backend-root-execution-unification-plan.md#p-run-core-001-batch-02)

> 状态：基于 `4996d25c` 的106行exact design已由design-critic最终PASS并经用户批准实施。source critic提出的accepted-before-body取消、descriptor/path metadata替换与sink provider降级覆盖三个阻断均已关闭，最终PASS且阻断/重要均为0；current tree的source、architecture、双variant unit、androidTest compile、native host/build、31个FREEZE zero-diff、6.6 AVD 23项与真实设备4项physical I/O门均已关闭。Host去重现已作为独立批获批实施；backend/UI与Diagnose后续批继续未授权，真实carrier/PRODUCT_ROOT继续`NOT_RUN`。

- [x] KTC-D0：核对 current caller、`git log --all -- <path>`、Android/PFD/File 反向依赖、资源 owner、失败语义与过度设计候选。
- [x] KTC-D1：冻结 coordinator/dispatch/presentation/sink 边界、immutable executor target、root-script single-open reader、106 行 exact ledger 与 source/AVD/设备判据。
- [x] KTC-D2：design-critic关闭KTC-CORE-01..06，最终总判PASS，阻断=0、重要=0、建议=0；机械ledger闭包六项均为0。
- [x] KTC-D3：用户在critic最终PASS后明确回复“批准”；授权仅覆盖本批106行ledger。
- [x] KTC-I1：按 exact ledger 单写入流实施，Host 去重、backend/UI 与 Diagnose 后续批不混入。
- [x] KTC-V1：architecture=`PASS/CORE_OUTER_DEPENDENCY=0/RETIRED_RUN_OWNER=0/FILE_ACTION_ROWS=598`；debug/release unit各155项且四桶全0；androidTest compile、native host 31/31、双variant build、31个FREEZE zero-diff与docs/UML一致性通过。
- [x] KTC-V2：6.6 AVD `emulator-5554`的physical I/O、staging/sink、App/Shizuku Diagnose与smoke、lifecycle/cancel/host-death共`OK (23 tests)`且无skip；三项新增provider用例覆盖private construction failure、optional MediaStore construction omission与逃逸setup异常前累计binding清理。
- [x] KTC-V3：真实non-root设备`b4541958`（`23013RK75C`、5.10.252、SDK 36）两个physical I/O exact class为`OK (4 tests)`且无skip；真实carrier/PRODUCT_ROOT保持`NOT_RUN`。

## 已完成批次：[P-RUN-X-001/批次03 App/Shizuku Host去重](backend-root-execution-unification-plan.md#p-run-x-001-batch-03)

> 状态：完成。91行exact ledger=`ADD=6 / MODIFY=15 / FREEZE=70`；shared controller、preparation adapter、Binder endpoint与thin entries均已落地。source critic的KTH-SRC-01/02/03全部关闭，最终`PASS / 阻断0 / 重要0 / 建议0 / 新增finding 0`；普通门、70个FREEZE零漂移与6.6 AVD五个exact class均已关闭。

- [x] KTH-D0：核对唯一active plan、历史、两份framework entry直接caller、AIDL/manifest identity、重复owner与workspace-create前PFD leak。
- [x] KTH-D1：KTH-HOST-01/02/03全部关闭；CBN、依赖方向、91行ledger机械闭包及最终定点复审全部通过。
- [x] KTH-D2：用户于2026-11-17在critic PASS后明确批准production implementation；批准不外扩到backend/UI/Diagnose后续批。
- [x] KTH-I1：只按91行ledger实施shared controller、preparation adapter、Binder endpoint与两个thin entry。
- [x] KTH-V1：architecture=`PASS/RUNHOST_OWNER_BOUNDARY=0/FILE_ACTION_ROWS=604`；debug/release unit各`200/200`、androidTest compile、native host`31/31`、双variant build、AIDL/frozen contract与docs/UML通过。
- [x] KTH-V2：6.6 AVD App/Shizuku smoke及既有lifecycle/cancel/host-death共`OK (5 tests)`且无skip；真实process-kill、carrier与PRODUCT_ROOT仍按原门保持`NOT_RUN`。

本批以后的原入口曾是`P-BACKEND-CORE-001/批次02`；它随后在批准前被完整 BackendPackage 新目标取代，未实施。过重的`P-BACKEND-X-002/决策01`又在用户认可前被精简；决策02虽曾获认可，但其exact batch在production前被新的Custom公开能力／raw-memory要求取代。current合法设计入口见下方`P-BACKEND-X-002/决策03`。

## 已完成：[P-RUN-X-001/批次04 执行域可读命名](backend-root-execution-unification-plan.md#p-run-x-001-batch-04)

> 用户明确要求两个framework entry的类名与文件夹层级都可直接识别执行权限，并裁定为M级、无需独立审查。最终采用对称的`runhost/appuid/AppUidRunHostService`与`runhost/shizukushell/ShizukuShellRunHostService`；只机械同步package、component/反射identity、caller、guard与current docs。

- [x] KTN-D1：最小计划已记录旧名→新名、保持项、caller与验证入口。
- [x] KTN-I1：两个类/文件、Manifest、App显式Intent、Shizuku class string、R8 keep、architecture路径和current文档已同步。
- [x] KTN-V1：architecture=`PASS/RUNHOST_OWNER_BOUNDARY=0/FILE_ACTION_ROWS=604/KEEP_DRIFT=0`；debug/release unit各`200/200`、native host`31/31`、androidTest compile及双variant build通过。
- [x] KTN-V2：6.6/4 KiB AVD启动Shizuku shell server并授予既有API权限后，App/Shizuku exact smoke最终为`OK (2 tests)`、exit=0且无skip；不外推为process-kill、root或PRODUCT_ROOT。

## 已取代、未实施：[P-BACKEND-CORE-001/批次02 backend package纯化](backend-root-execution-unification-plan.md#p-backend-core-001-batch-02)

> 69行same-APK package move曾完成自检与critic，但用户在批准前改为完整code-bearing BackendPackage；旧批没有production授权、没有实现、所有运行门`NOT_RUN`，ledger只留沿革且不得执行。

- [x] BPK-D3：以“未批准、被`P-BACKEND-X-002`取代”关闭，不产生实施授权。
- [x] BPK-I1/V1/V2：取消旧实现与旧门；没有运行结果，不记PASS。

## 已取代、未实施：[P-BACKEND-X-002/决策01 完整 BackendPackage 插件模型](backend-root-execution-unification-plan.md#p-backend-x-002-decision-01)

> 决策01完成过作者自检与critic，但用户在认可前指出终态机制和验证被错误地并入首批。其完整package、durable lease/journal、DCL/native与双域矩阵保留为研究沿革，不再是可批准入口；没有production授权或实现，所有门保持`NOT_RUN`。

## 已由决策03取代、未实施：[P-BACKEND-X-002/决策02与旧批次01](backend-root-execution-unification-plan.md#p-backend-x-002-decision-02)

> 状态：决策02曾完成作者自检、独立critic与用户认可，旧批次01也曾冻结Java ABI、KMP／native owner、统一i18n／settings和240行ledger；但在任何production授权或实现发生前，用户把Custom需求改为“可枚举并调用任意已加载plugin的公开能力，且可直接管理native memory或指定绝对地址写入”。这与旧filtered Custom、通用`ComponentContext`、Diagnose自用service registry及preparation/runtime registry耦合实质冲突。下列已关闭项只保留旧设计沿革，不能直接获批或拆行实施；当时critic的`CLOSED`不覆盖替代设计，全部产品门仍`NOT_RUN`。

- [x] BXP2-D0：用户纠正范围和验证广度；决策01以未批准／未实施被取代。
- [x] BXP2-D1：当前痛点、两类component、一跳input capability、Host ABI／target ISA分离、逐批顺序与按声明验证已写入唯一草案。
- [x] BXP2-D2：非作者design-critic只读审核确认可进入用户认可gate、阻断0；远期未实现及对应门未运行未被误作阻断。
- [x] BXP2-D2R：定点复审确认plugin-owned Kotlin native load/call owner与Host边界成立，阻断0；未受影响部分未重审。
- [x] BXP2-D2S：定点复审确认builtin/imported来源、身份、信任、seed/selection、卸载与rollback边界成立，阻断0；未受影响部分未重审。
- [x] BXP2-D2T：定点复审确认首次主进程启动、唯一owner、同一verifier/installer、readiness、幂等与崩溃可恢复性成立，阻断／重要／建议均为0。
- [x] BXP2-D1U/D2U：构建／module／文件层次作者自检与critic完成；唯一release-R8 shared-API保留阻断按原条件机械关闭，其余阻断0。
- [x] BXP2-D1V：Host capability registry增量作者自检完成；三投影、有限依赖闭包、filtered lookup、run-scoped factory及future custom复用已写入草案。
- [x] BXP2-D2V：同一非作者critic确认registry authority、有限依赖闭包、shared contract、NativeBinding lifecycle、两域typed graph与future custom边界成立；阻断／重要／建议均为0。
- [x] BXP2-D3：用户于2026-11-18在critic结论后明确认可；只开放批次01 exact design，不等于production批准。
- [x] BXP2-B1-D：原Android-library版exact API、package/manifest、catalog/seed、graph/wire、registry/loader/native与lifecycle已完成作者自检；其结论不覆盖KMP修订。
- [x] BXP2-B1-DIAG-RESULT-01：Lead按critic原条件核对并原样冻结current status/identity/log/handoff/cleanup三事实组合及outcome优先级；成功含义未改，机械关闭。
- [x] BXP2-B1-API-01：同一critic定点确认完整shared ABI、阶段异常与失败资源owner满足原条件，`CLOSED`。
- [x] BXP2-B1-APP-PROCESS-01：同一critic定点确认main/client death后的service-side独立进程终结owner与验证满足原条件，`CLOSED`。
- [x] BXP2-B1-C：原critic round 1全部3项关闭；KMP实质修订使原用户批准gate重新关闭。
- [x] BXP2-B1-KMP-D：KMP core／Diagnose Android source set／官方兼容toolchain preflight设计、228行scope与作者自检完成。
- [x] BXP2-B1-KMP-HOSTTEST-01：显式`withHostTest {}`创建host-test compilation，`androidHostTest`以test-only implementation取得API runtime，并验证不进入main runtime/D8/archive；preflight不再冒充证明尚未创建的KMP入口。
- [x] BXP2-B1-KMP-C：同一非作者critic只读审查KMP差异；唯一finding按原条件机械关闭，当前阻断0／重要0／建议0。
- [x] BXP2-B1-KMP-LAYOUT-01：按用户纠正将插件叶module统一收进`plugin/`并使用嵌套Gradle path；组织父project无源码／产物职责，各叶module沿用自己的`build/`；scope与实质设计不变。
- [x] BXP2-B1-NATIVE-OWNER-01：明确plugin包含core；`plugin/native-source`仅为不产公共DSO的源码authority，具体backend package独占DSO/load/call/close，`:app`的C/C++／JNA/JNI／project `.so`／`System.load*`／native handle owner全部为0。作者差异自检与非作者定点critic均完成，阻断0／重要0／建议0。
- [x] BXP2-B1-I18N-D：App／builtin plugin／imported plugin共用的KMP `I18nRegistry`、typed bundle identity、bounded catalog schema、Diagnose settings tree与`i18n/{en,zh}.json`、Host generic renderer、i18n4k薄门面及相称门禁已补入240行scope；现有41个Android UI caller的迁移另留P-UI。
- [x] BXP2-B1-I18N-ID-01：首轮critic要求把bundle identity绑定authenticated package identity；已改为`HOST_APP`／`PLUGIN(publisherId,packageId)`并补同packageId不同publisher、App伪装、same-owner update与self-only remove判据，同一critic定点复审确认`CLOSED`。
- [x] BXP2-B1-I18N-FALLBACK-01：按critic原条件机械冻结ordered multi-locale候选；全部active locale的exact／script／language候选耗尽后才用bundle default，补`[fr-CA,zh-Hans]→zh`与去重／最终default unit判据。
- [x] BXP2-B1-I18N-C：同一非作者critic确认identity finding关闭及fallback机械关闭有效，unified registry／catalog transport／Host renderer owner差异最终阻断0／重要0／建议0。
- [x] BXP2-B1-A：取消旧production gate；决策03已在批准前取代本批，旧设计不得再直接获批实施。
- [x] BXP2-I1/V1：取消旧实现与旧门；无授权、未实施，所有产品门`NOT_RUN`且不得记PASS。

### 资源准备／动态选项路线（历史候选；已并入决策03）

> 用户比较backend自行枚举provider并接UI与Host反射／registry注入两条路线，并追问动态选项。当前候选采用第三条路线：Host拥有revisioned准备草稿、provider binding选择、调度、失效与generic renderer；provider拥有导入／extract／build／验证／provenance语义；backend只声明requirement并在run cutoff后从filtered lookup按slot消费冻结结果。cutoff前可在manifest预声明集合内调整选项、候选和步骤，旧revision结果丢弃；accepted run不轮询registry、不反射注入、不热换provider／artifact。复杂preparation API／store／execution domain／状态机留真实profile／extractor／KO consumer批次，不扩大批次01 exact Java ABI、240行ledger或门禁。

- [x] BXP2-D1W：作者沿current UI→cutoff→freeze→RunHost prepare调用链完成差异自检。
- [x] BXP2-D2W：同一非作者critic确认可进入用户认可gate，阻断0／重要0／建议0。
- [x] BXP2-D3W：取消独立认可gate；该候选未获用户认可，方向由决策03 preparation plane吸收并与通用runtime registry解耦。

## 当前有效设计，独立审查及用户认可完成：[P-BACKEND-X-002/决策03 分平面插件体系与特权Custom能力目录](backend-root-execution-unification-plan.md#p-backend-x-002-decision-03)

> 决策03保留一个authenticated package catalog／exact identity权威，但把package/catalog、presentation/i18n、preparation/artifact、ordinary backend run、privileged Custom分成职责平面；真正传给plugin code的只有preparation job、ordinary backend run、Custom execution三类context。`.glplugin.zip`继续原子组包`BACKEND`／`PROVIDER`／`CUSTOM`；export分immutable value、finite job、live service，并以`PRIVATE`／`DEPENDENCY`／`CUSTOM_CALLABLE`控制可见性。同package private native直接调用，ordinary跨包依赖必须声明且不可枚举，Custom在专用可丢弃RunHost中枚举本session已加载package明确公开的capability，可通过stable raw-memory／operation contract管理owned native memory或向指定绝对地址读写；Host不暴露reflection、JNA Pointer／Native.invoke或任意JVM对象，plugin Kotlin／private DSO继续独占load／call／close。preparation由Host拥有revisioned draft／调度／freeze，provider以有限job发布immutable Document／Artifact；动态UI字段仍是Host draft数据，不变成provider node。builtin/imported同路、thin archive、KMP、Host零native owner、统一`I18nRegistry`与declarative settings均保持。通用extractor的计划、源码、Capstone/native和Maven发布权威已迁移到独立`NickJi2019/KernelSymbolExtractor`；历史G0/K0/G1保留identity证据，用户2026-11-19在production前以Java-object/JVM/no-CLI方向取代旧KMP／五坐标／canonical-wire topology。replacement最终审查阻断0／重要0／建议0，用户只批准G1R；独立库现为`:api/:engine/:android/:jvm`，四坐标local metadata/checksum、20/20固定fingerprint签名演练及空缓存Java/JVM/Android consumer均通过，G1R关闭。production Java/Kotlin/C仍为0，K1、Maven Central publication与本仓consumer均未授权／未开始。本仓原计划路径只留指针，未来真实consumer只直接固定版本引用Android facade，不恢复同仓module。

- [x] BXP3-D0：新增Custom硬需求、旧设计冲突及“完全体过度设计”问题已写入替代草案。
- [x] BXP3-D1：作者沿current disabled Custom／empty Lua catalog、Diagnose直接binding、BackendUi Android Compose及UI→cutoff→freezer→RunHost调用链完成自检；已修正Custom export依赖闭包、session package选择、DCL保持项与Host／plugin私有类表述。
- [x] BXP3-D2：既有非作者design-critic按固定SHA只读审核决策03且未复用旧PASS；总判可进入用户认可gate，阻断0／重要0／建议0。
- [x] BXP3-D3：用户于2026-11-19在critic阻断／重要／建议均为0后明确回复“认可”；只关闭设计认可gate，不构成production批准。
- [ ] BXP3-B1：初版exact design及可编辑`.glplugin.zip`格式差异均已完成作者自检与同一非作者critic定点复审，最终阻断／重要／建议／新增finding均为0；用户于2026-11-20明确批准production implementation，P0 KMP/AGP单变量preflight已通过，现进入148行ledger的production source，不复用旧240行。
- [ ] BXP3-I1/V1：production已授权且P0通过；source implementation刚开放，architecture、完整unit、native、artifact、AVD与真机门仍`NOT_RUN`，Desktop不在本批范围。

当前顺序为：

1. 决策03作者自检、独立critic与用户认可均已完成；
2. 当前最小exact vertical只含package/catalog与builtin Diagnose package的私有native直调，不制造同包service registry；ZIP格式差异审查与production批准已关闭，P0单变量preflight已通过，现按148行ledger进入production source；
3. 以data-only profile provider证明独立更新／回滚且无需重装APK；
4. 由真实Custom consumer冻结可枚举能力目录和首个raw-memory／operation contract；
5. 再按真实caller逐批迁移43499、KO／43284、98260和新增架构；extractor consumer另批只直接引用已发布、固定且校验身份的`kernel-symbol-extractor-android` Maven版本，先report-only，不复制源码／submodule／composite／AAR，也不回退旧Rust；caller自行负责report/expectation objects的持久化／格式转换／导出，并在重建objects前先验证自有authenticated envelope真实性／完整性，library semantic verifier不替代该门；远期PC／OCI／TUF／toolchain按触发细化，不阻断较早批；
6. plugin owner稳定后，再细化`P-UI-CORE-001`的`:app:shared` KMP UI、App base catalogs→唯一`I18nRegistry`、`:app:android`／`:app:desktop`平台壳，并与`P-DIAG-NATIVE-001/批次02`重新排定。

## 上述 Kotlin owner 批完成后重新细化的暂缓门：[P-DIAG-NATIVE-001/批次02：并发source churn与>10 MiB压力](backend-root-execution-unification-plan.md#p-diag-native-001-batch-02)

> 状态：v4 producer/lifecycle部分保留；旧通用GLK schema/repository/JCS `PROFILE` freeze与51行scope不再授权。当前没有`stress_enabled`真实consumer，也没有预建Diagnose store；P-DIAG未来同批拥有Diagnose具名typed DataStore、per-run immutable snapshot、跨进程承载和Flow消费，并重新复审/批准。production仍为`NOT_RUN`，不能拿旧critic PASS直接实施。

- [x] AS-R0：记录native线程+Kotlin协程并发、运行中停旧增新、动态FD connector、>10 MiB无累计上限、drain后native handoff及产品false。
- [ ] AS-D1R：producer/lifecycle/ABI/故障矩阵候选保留；Diagnose具名typed setting、per-run snapshot与逐文件ledger等待四个 Kotlin owner 批按批准顺序完成后，再以最终路径重生并复审；旧通用GLK/JCS配置切片不得实施。
- [ ] AS-I1：实现获批的producer churn与Flow条件step；开关关闭时steps中不存在stress step。
- [ ] AS-V1：4/16 KiB短链不回归，6.6 AVD App/Shizuku stress达到>10 MiB、动态attach/detach、三sink同commit order且产品false。
- [ ] AS-V2：最终树执行 `RunHostLifecycleTest`、`RunHostCancellationTest`、`RunHostHostDeathTest` exact AVD class。
- [ ] AS-V3：具名真实Android设备完成automatic-export exact bytes/order/finalization与最终产品验收。
- [ ] AS-V4：真实carrier设备门（三.2）保持 `NOT_RUN`。

## 已完成本批：[P-RUN-HANDOFF-001/批次01：common 固定位置 handoff](backend-root-execution-unification-plan.md#p-run-handoff-001)

- [x] HND-D0/D1/D2：唯一active plan、固定位置/marker、same-opened fd、单次native chain、multi-fact owner与用户批准均已关闭；JNA同版发布接口按用户纠正删除version、size、reserved、layout/header检查及旧兼容分支。
- [x] HND-I1：common `run_handoff()`、product/diagnostic publisher、两端accepted-start `GHOSTLOCK_HOME`、Flow multi-fact、唯一`gl_diagnose_run`、双pipe binding、单step Diagnose、source guard、exact ledger与current UML已接通；真实攻击backend零改动且保持断开。
- [x] HND-V1：`nativeTestContractsHost`=`3/3`、Flow/Diagnose/root-script聚焦unit=`19/19`、debug/release exact unit各`111/111`、`nativeTestHost`=`31/0`、architecture=`ARCH_VIOLATIONS=0`、clean后`nativeBuildAndroid`双variant/JNA distribution PASS；6.6 4 KiB/16 KiB AVD的App/direct-JNA/Shizuku full exact class各为`OK (3 tests)`且无skip。

## 已完成本批：[P-LOG-X-001/批次02：全流式双流与独立 sink 积压](backend-root-execution-unification-plan.md#p-log-x-001-batch-02)

- [x] 用户选定raw/processed双流尽快处理、各sink自行积压、`RunReport`摘要只作为普通Info日志且服从全局阈值；未实施的v2.1 chunk/journal/viewport/final-copy candidate已失效。
- [x] v3 exact ledger=56行（含6个文件更名、9个额外caller/夹具）；单processor、单dispatcher、UI/private-file/可选MediaStore的独立unlimited sink queue、GLLOG2无隐藏typed report trailer与active share barrier已冻结。
- [x] v3 design-critic两轮关闭V3-01..03，最终阻断=0；UI采用append-only ArrayList+O(1)只读prefix snapshot，旧run/clear/barrier以同一FIFO control union终结。
- [x] LS-D4：用户明确批准v3实施并要求收敛非必要RunLog*命名；定点复评新增架构阻断=0，56行ledger已纳入更名与额外caller/夹具。
- [x] LS-I1：56行exact scope源码已完成，包含日志专用`RunLog*`更名、双流/独立sink、GLLOG2、完整UI快照与文件前缀分享；旧累计256/512、whole-document ByteArray及逐行flush不再属于current source。
- [x] LS-V1：debug/release unit各104/104、architecture、native host 30/0、>10 MiB Android端MediaStore逐字节一致与App/Shizuku 4 KiB AVD smoke均PASS；按用户修订，双 variant build/JNA distribution 取代已删除的 native artifact verifier 并PASS。
- [x] LS-I2：用户免评审直接授权的统一sink更正已落地；UI/private-file/MediaStore由factory列表创建并由binding列表按role分发，UML与caller已同步。
- [x] LS-V2：更正后104项双variant unit、architecture、native host与4 KiB AVD 6+1+1项PASS；修订后的`nativeBuildAndroid`双 variant build/JNA distribution 门PASS。
- [x] LS-I3：用户再次简化为预构造的存储sink直接传入`LogHub.start`，UI在接受后补入同一binding列表；拒绝run会abort且worker未做平台I/O。LS-I2的factory注入形态已被取代。
- [x] LS-V3：最终树debug/release各104项unit、native host 30项、architecture与4 KiB AVD storage/UI 7项及App/Shizuku Diagnose各1项PASS；修订后的`nativeBuildAndroid`双 variant build/JNA distribution 门PASS。原ELF/ABI/JNI verifier已删除而非PASS。

## 已由后继证据关闭：[P-LOG-X-001/批次01：单一日志中枢](backend-root-execution-unification-plan.md#p-log-x-001-batch-01)

- [x] `LogHub` 成为唯一日志中枢；Kotlin/Native/Shell/RunHost records 在同一 lock 内按 acceptance commit order 排序、统一 threshold、统一 fan-out。
- [x] `Flow.run` 在中立 terminal claim 后返回最终 `RunReport`；App/Shizuku RunHost service 只调用 Flow 与转运，不再各自组合 report。
- [x] event-as-log、独立 `RunReportRows` 与 legacy `app/log` singleton/file owner 已删除；Material3/Miuix 共用五级默认色与有界 ANSI foreground renderer。
- [x] current exact unit gate PASS：`UNIT_TEST_XML=PASS FILES=33 TESTS=96 FAILURES=0 ERRORS=0 SKIPPED=0`；architecture inventory PASS 且 `UNCLASSIFIED=0`。
- [x] current P-LOG-X-001/批次01 androidTest compile、native host与Android zero-target gates已按 current tree通过；clean 后 CMake 外部 manifest 需用 AGP build-model 参数重新 configure 的 producer dependency另记 build debt，不改变 gate 的最终 PASS 输出。
- [x] 2026-11-07账本收口：P-LOG-X-001/批次02 LS-V3已在current tree通过4 KiB AVD storage/UI 7项及App/Shizuku各1项；P-RUN-HANDOFF-001/批次01 HND-V1又在4 KiB/16 KiB AVD通过App/direct-JNA/Shizuku full exact class各3项。两组后继证据共同覆盖批次01的current LogHub wire/UI/runtime欠账；这只关闭旧批次证据债，不代表automatic export真实设备门或最终产品验收完成。

- [x] P-LOG-IO-001/批次02明确取代P-LOG-IO-001/批次01 runtime topology；P-LOG-IO-001/批次01恢复的settings、GLLOG1、canonical/manual-share与partial-file产品语义继续保留。
- [x] design-critic已关闭v0.3与P-LOG-IO-001/批次02全部阻断；single-channel/single-worker sink已删除opening、double queue、sequence/drainer、timeout与completion monitor。
- [x] domain contract、Android worker、LogHub、composition/caller、四组有效test source、architecture inventory与UML已迁移到P-LOG-IO-001/批次02；无真实双client路径的标签式parity test已按“严防过度设计”删除，本项只表示source交付。

### P-LOG-IO-001/批次01 被取代历史（不再作为 current runtime architecture）

- [x] 用户纠正 automatic export：它是未完成能力，不是删除项。
- [x] 在唯一活动计划、exact ledger 与 UML 中恢复产品责任，并保留 R4 已发生删除的沿革。
- [x] design-critic round 4 已关闭 run-scoped owner、逐行 flush、typed formatter、failure semantics、producer ordering 与 exact file rows；阻断/重要/建议均为0。
- [x] 用户以“批准R4A和R5”批准R4A具体结构；已同批接通 settings → LogHub → MediaStore exporter，没有恢复无caller UI。
- [x] manual share与automatic export已共用唯一GLLOG1 formatter；四组有效test source与architecture current fixture已登记，RunHost client的sink零ownership改由source guard锁定。该 checkbox 只表示 source 交付，不表示 automatic export 产品项完成。
- [x] R4A-S5 已关闭abort与backend同步返回竞争窗：active slot在`finally`结束前不释放，`NonCancellable`幂等终结保证`Aborted` trailer与single close；该回归先进入R4B历史93项基线，现继续包含在R4C current 96项unit exact PASS中。
- [x] R4A-S6 已关闭export worker退出与producer enqueue竞争窗：closed channel不再伪报overflow，worker failure不再留下可接收但无consumer的command channel；R4B sink exact instrumentation 7项已覆盖normal close、failure与late-write拒绝并PASS。
- [x] R4A-S7 的opening/claim topology已由R4B整体删除，current single-channel sink以blocked-open finish/drain测试取代；对应exact instrumentation已PASS，不保留旧duplicate-await判据。
- [x] R4A-S8曾把新增abort unit method投影为168→169；R4B source复评后的历史基线为32个test source、93个`@Test`。R4C 新增 wire-value、ANSI renderer 与 central threshold/origin 判据后，current tree为33个XML suite、96项 test，`expectedTests=96`且 exact gate重跑PASS。
- [x] R4A-S9 已移除无收益的本地BufferedOutputStream并修正failure文件合同：成功文件仍逐行完整，失败文件只保证canonical byte prefix，provider故障可留下不完整最后一行；partial-write exact instrumentation已PASS。
- [x] source architecture verifier已经两轮失败定位与定点debug后PASS；`RUNTIME_REFLECTION=0`、`UNCLASSIFIED=0`、`REMOVE_RESIDUE=0`、`KEEP_DRIFT=0`。
- [x] Gradle debug compile/unit XML exact gate已按 R4C current tree通过：`./gradlew clean --console=plain && ./gradlew :app:testDebugUnitTest :app:verifyDebugUnitTestXml --no-build-cache --rerun-tasks --console=plain`，exit=0，literal=`UNIT_TEST_XML=PASS FILES=33 TESTS=96 FAILURES=0 ERRORS=0 SKIPPED=0`。普通缓存命令曾从污染的Gradle资源缓存恢复带空格的duplicate packaged resources；clean + no-build-cache/rerun排除source问题并在fresh outputs通过。
- [x] native host=`29/0`、Android zero-target build=`COUNT=0 ABI=none JNI=0`、androidTest compile、sink=`7/0/0/0`、settings=`4/0/0/0`、App RunHost=`1/0/0/0`与AVD真实MediaStore automatic/manual bytes=`1/0/0/0`均已通过。
- [x] 用户提供Shizuku AVD production runtime证据：`uid=2000`、`domain=u:r:shell:s0\0`、Diagnostic PASS、primitive SUCCESS、script/payload/finish SKIPPED、cleanup SUCCESS、terminal DIAGNOSTIC_COMPLETE、product success NO；命令/exit未提供，因此只按用户观察的runtime evidence记录，不补造自动化结果。
- [ ] 真实设备门与最终产品验收仍为NOT_RUN。
- [ ] automatic export 通过上述 gates 并获最终验收后，才可从 UNFINISHED 改为完成；禁止以删除旧 `LogExport.kt` object 冒充能力完成。

## 随后：偿还 P-RUN-IO-001/批次01 至 P-LOG-X-001/批次01（原批次范围） evidence debt

- [x] R2-H1 Android发布纠错已在`GhostLock_6_6`完成：kernel 6.6.30，Payload Save成功，App未崩溃，published script/result mode与无temp残留符合合同；只关闭SELinux hard-link故障，不代表下项完整矩阵通过。
- [x] 执行 P-RUN-IO-001/批次01 root-script fault matrix 与 ordinary-file identity/staging matrix；host=`31/31`、6.6 AVD=`3/3`、architecture=`D0_VERIFY=PASS ARCH_VIOLATIONS=0`，三个命令均exit 0；真实carrier设备门仍单独保持`NOT_RUN`。
- [x] P-RUN-X-001/批次01 Kotlin compile、debug/release exact unit各`111/111`、typed App/Shizuku正常transport/runtime已由P-RUN-HANDOFF-001/批次01 current-tree证据覆盖；4 KiB/16 KiB AVD的App/direct-JNA/Shizuku full exact class各为`OK (3 tests)`。
- [x] P-RUN-X-001/批次03在6.6 AVD同一exact命令补齐`RunHostLifecycleTest`、`RunHostCancellationTest`、`RunHostHostDeathTest`，并与App/Shizuku smoke合计返回`OK (5 tests)`、exit=0；这关闭专属class欠账，不外推为真实process-kill。
- [x] 执行 P-RUN-CORE-001/批次01/P-LOG-IO-001/批次02 architecture source guard；exact command exit=0，literal=`D0_VERIFY=PASS ... ARCH_VIOLATIONS=0 ... OLD_AIDL_TRANSACTION=0 MANUAL_JNI_OBJECT_MARSHALLING=0 ... SUPERSEDED_RUN_LOG_TOPOLOGY=0 RUNHOST_CLIENT_LOG_SINK_OWNER=0 ... UNCLASSIFIED=0`。
- 证据纪律：失败保留为失败并回到debug；未执行项只记`NOT_RUN`，不用source implementation冒充PASS。本句是执行规则，不作为开发交付checkbox。
- [x] lifecycle/cancel/host-death专属class欠账已由P-RUN-X-001/批次03 current tree偿还；后续package/producer批改变最终路径后，AS-V2仍负责重跑最终树regression。真实process-kill与carrier设备门继续`NOT_RUN`，当前所有真攻击backend均已断开。

## Source implementation 已完成

1. E-DOC-X-001/批次01/P-BACKEND-X-001/批次01：authority 收口与 disabled/original backend closure 删除。
2. P-RUN-IO-001/批次01：Payload/Finish v3 repository、唯一 renderer、published artifact 与 typed result codec。
3. P-RUN-X-001/批次01：neutral registry/plugin/Flow、typed RunHost、App/Shizuku clients、workspace/staging、六层 RunReport 与 UI projection。
4. P-RUN-CORE-001/批次01 source已完成：caller-zero SessionPhase、source guard 与非导出 pointer；日志产品验收由后继P-LOG批次承接，不再把本批写成partial source。
5. P-LOG-IO-001/批次02：single-channel/single-worker automatic-export source与first-failure callback。
6. P-LOG-X-001/批次01：唯一日志中枢、Flow-owned RunReport、typed log wire、统一 ANSI-aware UI与current docs pointer；旧current-tree AVD欠账已由批次02与Handoff后继证据关闭。

automatic export 不在“已完成删除”清单；P-LOG-IO-001/批次02 负责唯一 I/O sink，P-LOG-X-001/批次01 负责其上游唯一日志中枢，当前产品状态仍为 UNFINISHED。

## 已完成：[P-DIAG-NATIVE-001/批次01：Diagnose native/JNA/shell纵向](backend-root-execution-unification-plan.md#p-diag-native-001-batch-01)

- [x] R6-I1：固定JNA 5.19.1与许可资产，加入真实`libghostlock_diagnose.so`、ordinary-C ABI、host contract test及debug/release双variant artifact verifier。
- [x] R6-I2：以release-visible `DIAGNOSE`替换`DEBUG-PROBE`，完成constructor-load candidate的原子PREPARED发布、`SequentialFlow`两step、Native/Shell日志与handoff shell全链路；6.6/4 KiB AVD上的App production adapter→LogHub纵向已输出`OK (1 test)`。
- [x] R6-I3：完成`LogHub`机械改名、current docs/UML与三份进度投影的唯一authority收口。
- [x] R6-V1：debug/release 34 files/102 tests、native host/Android均已PASS；6.6/4 KiB与6.6/16 KiB AVD的debug exact class gate都以`OK (3 tests)`/`SKIPPED=0`覆盖App/direct-JNA/Shizuku，两个page size下的App与Shizuku minified-release production UI也均已PASS。AVD Diagnose PASS只证明诊断纵向，不证明root或CVE。

P-DIAG-NATIVE-001/批次01是已完成的非攻击诊断批；P-BACKEND-NATIVE-001/批次01与P-BACKEND-NATIVE-002/批次01仍未排期，真攻击backend保持断开。

## 真攻击 backend 断开收口

- [x] R5-S2 native carrier与R5-S3 Disabled plugin实施已取消；旧 R5 carrier child（已终止）只保留沿革。
- [x] Kotlin registry中真实攻击backend数量为0。
- [x] Gradle不再接受backend/M0开关或运行43284 M0 source staging/build task。
- [x] app CMake backend assembly不再声明option或include 43499-Glued、43284-Glued、98260 chunk。
- [x] native artifact verifier把header-only manifest解释为断开权威，并要求merged native output实际为空；不再以“至少一个target”反向强迫接回攻击ELF。
- [x] 旧native tree保持source-only，不修改其攻击、handoff、waiter/race、restore或park实现。

P-BACKEND-NATIVE-001/批次01 / P-BACKEND-NATIVE-002/批次01不再是自动排队的future child。未来若用户重新批准任一真实攻击backend，必须新建L级计划并明确取代本次断开裁定；已完成的P-DIAG-NATIVE-001/批次01仅覆盖非root诊断链。

</details>
