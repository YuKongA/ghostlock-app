# RunHost framework owner

`runhost/` 在两个对称的执行域叶目录中保留thin framework entry：`appuid/AppUidRunHostService`表示package application UID的`:flowhost`入口，`shizukushell/ShizukuShellRunHostService`表示Shizuku shell UID入口。该目录不是 backend implementation package，也不拥有第二份 backend membership 或 wire 合同。

当前 owner 划分：

- `core/runhost/`：platform-neutral protocol、request/snapshot model、one-shot state machine与唯一`RunHostController`；
- `core/run/`：application `RunSessionCoordinator`、fixed-target dispatch/sink port与actor cutoff；
- `core/log/`：唯一 `LogHub` 与 Kotlin connector；
- `data/runhost/client/`：App/Shizuku Binder client、snapshot poller 与 callback log connector；
- `data/runhost/client/AndroidRunHostDispatchAdapter`：input freeze、AIDL assembly和固定App/Shizuku target的唯一application dispatch owner；
- `data/runhost/wire/`：generated AIDL Parcelable 与 core model 之间的唯一codec/Parcel budget owner；`RunHostBinderEndpoint`还独占callback wire与六个business delegate；
- `data/runhost/host/`：`AndroidRunHostPreparationAdapter`独占role/live context、validated descriptor接管、workspace/staging与plugin loader编排；`ProductRunPreparer`只编排pure plan mapper、physical publisher和bound result reader；
- `data/runhost/plugin/`：archive重验、0400 extract、filtered parent、`DelegateLastClassLoader`、shared API entry检查、`PluginBackendFlow`和one-shot `PluginRunResourceOwner`；
- `data/rootscript/`：product/diagnostic脚本物理发布、ordinary-file identity与单descriptor有界GLR1读取；
- `data/log/`：与 backend 无关的 FD 日志 connector；
- `runhost/appuid/AppUidRunHostService`：App UID + app SELinux domain的thin framework entry，本地保留`Service/onBind`与物理`killProcess`；
- `runhost/shizukushell/ShizukuShellRunHostService`：Shizuku shell UID 2000 + `u:r:shell:s0`的thin framework entry，本地保留constructors/live context/`System.exit(0)`。

两个 framework entry 复用同一 `app/src/main/aidl/com/ghostlock/app/run/` 合同，不读取catalog、APK asset、UI presentation或backend-private class，也不建立第二份membership。六个business override只委托同一endpoint/controller路径；第七个framework `destroy()`在controller确认DESTROYED且物理owner静止后提交一次进程退出。App entry在当前transaction返回后执行`stopSelf()+killProcess`，Shizuku entry执行`System.exit(0)`；两者都不把unbind或service stop冒充进程死亡。

`RunHostController`是lifecycle、candidate、callback/snapshot、cancellation、Flow worker、log sequence/drop与唯一`inFlightPhysicalOperations + currentEpochCompletion`的owner。candidate中的execution lease先关闭plugin run，再允许workspace cleanup；materializer、worker、safe stop和cleanup登记同一epoch，所有物理调用与barrier等待都在controller mutex外。因此RUNNING未claim时close立即返回`UNSAFE_TO_DESTROY`，其余允许close的状态也不得越过最后访问者。endpoint不拥有framework destroy，preparation adapter不拥有lifecycle/callback/snapshot。

archive PFD与ordinary artifact PFD沿同一request进入，但owner不同。Binder endpoint在每个prepare出口关闭decode PFD；wire codec先验证archive为只读ordinary file，再产生validated duplicate holder。holder把ordinary PFD一次性转移给staging、把archive一次性转移给loader，之后只关闭未转移余项。staging与loader各自只关闭自己取得的descriptor，不能由Host或plugin重复接管。

application侧不再有混合的`AndroidRunSessionAdapter`：coordinator在同一次READY catalog view中固定descriptor、exact `PluginObjectRef`、executor与actor cutoff，dispatch adapter持有request descriptor lease；LogHub拒绝或pre-commit失败由application关闭lease，accepted commit后由execute closure终结。client在terminal或prepare rejection后依次完成log drain、`requestClose`、`destroy`和unbind，再等待business binder death；5秒内未观察到death时返回transport failure并quarantine原binder，直到迟到death解除。PRODUCT_ROOT的Flow仍只看到neutral `RootScriptResultReader`，不会接触`File`、descriptor或`RootScriptResultCodec`的physical入口。

当前plugin增量只在既有AIDL request上增加archive PFD与semantic content root；business method set、其余字段顺序、profile形状、Payload/Finish与产品成功语义保持不变。当前唯一package是builtin ARM64 Diagnose；provider、import、Custom、真实攻击backend和额外ABI不在本目录中预实现。
