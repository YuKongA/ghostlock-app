# Backend membership 与 builtin Diagnose plugin

> 本文描述当前工作树。有效设计见 [BXP3-B1](../../../../../../../../tools/engineering/openspec/changes/builtin-diagnose-plugin/design.md#record-gl-dek)，当前结构图见 [full-process-uml.md](../../../../../../../../docs/development/full-process-uml.md#global-current-view)。历史 same-APK `BackendRegistry`、`BackendDefinition`、Host-owned Diagnose Flow/JNA/native 与 Custom placeholder 已退出当前实现。

## 1. 当前 owner

~~~text
plugin/
  api/                         Host 与插件共用的 Java diagnostic-v1 ABI
  core/                        KMP manifest、index、catalog 与 identity 模型
  packages/diagnose/           Diagnose Kotlin、JNA、C/C++ source 与 package producer

app/src/main/kotlin/com/ghostlock/app/
  domain/backend/              neutral BackendDescriptor 与 availability
  composition/plugin/          PluginCatalogCoordinator
  composition/backend/         BackendCatalogProjection
  data/plugin/                 archive verifier、immutable object store、seeder、object PFD source
  ui/backend/                  Host presentation；不能创造 membership
  data/runhost/plugin/         RunHost reverify、DCL、shared-API bridge 与 execution lease
  core/flow/                   Host Flow 合同；PluginBackendFlow 是插件结果适配器
~~~

`BackendDescriptor`只保存stable key、selection、abort、execution domain与capability，不持有Flow、Android Context、插件class、PFD、path或persistence callback。插件manifest是descriptor事实来源；Host presentation registry只把已存在的catalog key映射到Android UI。

## 2. Membership 与选择

当前唯一installed package是APK builtin、ARM64、单BACKEND component的`DIAGNOSE`：

- selection key固定为`DIAGNOSE`；
- capability固定为`DIAGNOSTIC_ONLY`，不执行Payload/Finish；
- Kotlin entry、JNA class、`libjnidispatch.so`与`libghostlock_diagnose.so`全部由package拥有；
- Custom、provider、external import、真实攻击backend和额外ABI均未实现，不以placeholder占据membership。

启动顺序为`BuiltinPluginSeeder → AndroidPluginCatalogStore → PluginCatalogCoordinator → BackendCatalogProjection → selection restore`。catalog只发布一个terminal `READY`或`FAILED` generation。projection从同一record同时产生descriptor与exact `PluginObjectRef`；UI只读取descriptor，run cutoff取得同源object ref。重复backend key、重复完整component identity或非法descriptor使整次bootstrap fail closed，不按排序选胜者。

`BackendSessionStore`持有`FRESH_UNINITIALIZED/SELECTED/NEEDS_USER_CHOICE`与nullable key。explicit builtin default只用于fresh状态；缺失、disabled或不兼容的旧选择进入user choice，不回退到列表第一项。catalog失败向页面投影bounded reason并关闭run gate，不插入synthetic backend row。

## 3. Package 与执行链

`.glplugin.zip`是普通ZIP/ZIP64。manifest的`files[]`用SHA-256覆盖除manifest本身外的每个regular file，normalized typed manifest产生semantic content root；ZIP entry顺序、timestamp或压缩变化不改变逻辑身份。CRC只检查容器意外损坏。三份JNA许可证是普通`files[]` entry，不存在JNA inventory、AAR/POM hash或许可证专项identity。

一次运行按以下owner链执行：

1. `RunSessionCoordinator`从同一READY view冻结descriptor、exact object ref、executor及Operational/Payload/Finish cutoff。
2. `AndroidRunInputFreezer`打开catalog object的read-only archive master PFD；dispatch lease拥有request duplicate。
3. Binder endpoint在每个prepare出口关闭decode PFD。wire codec验证archive是read-only ordinary file，再产生validated duplicates。
4. preparation adapter把ordinary PFD一次性转移给staging，把archive PFD一次性转移给loader；workspace只在descriptor acquisition成功后创建。
5. loader复制archive后立即关闭PFD，重验ZIP、manifest、全部file hash、content root、DEX/ELF、API/SDK/ABI/domain/capability，再以filtered parent和`DelegateLastClassLoader`加载entry。
6. `PluginRunResourceOwner`在首次class lookup前标记process taint，登记唯一`DiagnosticBackendRun`并管理TCCL与idempotent close。`PluginBackendFlow`把shared API结果转换为Host facts。
7. controller先终结execution lease，再按retention判据清理workspace；client完成log drain、`requestClose`、`destroy`与unbind后，必须观察business binder death才能接受terminal/rejection。

main process不加载插件DEX、JNA或native DSO。RunHost不读取catalog、APK asset或provenance；builtin与未来imported object进入catalog后的执行路径必须相同。

## 4. 结果与日志边界

`RunReport`是结果authority。产品成功只允许`runCapability=PRODUCT_ROOT`且`terminal=SUCCESS`；Diagnose即使得到`diagnosticOutcome=PASS`与`DIAGNOSTIC_COMPLETE`，`isProductSuccess`仍为false。raw uid 0、process exit 0、stdout、callback、单项primitive或日志都不能升级产品成功。

插件只通过窄`DiagnosticRunContext`发送Kotlin日志；Native/Shell pipe由Diagnose package消费并转换为shared API facts。Host的`KotlinLogConnector`、callback connector与application `LogHub`仍负责全局日志提交和sink fan-out。日志是diagnostic sink，不是result authority。

## 5. 资源与终结

- catalog object：main process store owner；run只取得read-only master PFD，不接触path；
- request duplicate：application dispatch lease owner；拒绝或execute结束时幂等关闭；
- Binder decode PFD：endpoint `finally` owner；
- validated ordinary PFD：staging owner；validated archive PFD：loader owner；
- extracted package与classloader：one-shot RunHost process owner；不伪造native unload；
- plugin run：`PluginRunResourceOwner`恰好一次close；
- workspace：`AndroidRunWorkspace`唯一物理删除owner，execution lease结束后才可删除；
- process：App entry以`killProcess`、Shizuku entry以`System.exit(0)`终结；client以binder death而非unbind/stop作为完成证据，超时则quarantine原binder直到迟到death。

## 6. 后续边界

- 43284-Glued、43499-Glued与98260保留source-only candidate，未进入plugin package、catalog或APK active native graph。
- Provider/Document、external import/update、READY后热更新pin/lease、Custom/raw memory、Desktop、统一KMP i18n与非ARM64 thin variant等待各自真实consumer批。
- 当前`PluginBackendFlow`保留Host `Flow`编排合同；删除或替换Flow是另一项设计差异，不能借插件化顺手实施。
- 唯一当前全局结构图维护在 [full-process-uml.md](../../../../../../../../docs/development/full-process-uml.md#global-current-view)；本文只解释backend/plugin owner，不复制wire字段表或完整验收矩阵。
