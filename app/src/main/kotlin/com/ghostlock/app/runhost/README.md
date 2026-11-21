# RunHost framework owner

`runhost/` 在两个对称的执行域叶目录中保留thin framework entry：`appuid/AppUidRunHostService`表示package application UID的`:flowhost`入口，`shizukushell/ShizukuShellRunHostService`表示Shizuku shell UID入口。该目录不是 backend implementation package，也不拥有第二份 backend membership 或 wire 合同。

当前 owner 划分：

- `core/runhost/`：platform-neutral protocol、request/snapshot model、one-shot state machine与唯一`RunHostController`；
- `core/run/`：application `RunSessionCoordinator`、fixed-target dispatch/sink port与actor cutoff；
- `core/log/`：唯一 `LogHub` 与 Kotlin connector；
- `data/runhost/client/`：App/Shizuku Binder client、snapshot poller 与 callback log connector；
- `data/runhost/client/AndroidRunHostDispatchAdapter`：input freeze、AIDL assembly和固定App/Shizuku target的唯一application dispatch owner；
- `data/runhost/wire/`：generated AIDL Parcelable 与 core model 之间的唯一codec/Parcel budget owner；`RunHostBinderEndpoint`还独占callback wire与六个business delegate；
- `data/runhost/host/`：`AndroidRunHostPreparationAdapter`独占role/live context、execution-domain/readiness、PFD/workspace/staging/publication与fresh Flow构造；`ProductRunPreparer`只编排pure plan mapper、physical publisher和bound result reader；
- `data/rootscript/`：product/diagnostic脚本物理发布、ordinary-file identity与单descriptor有界GLR1读取；
- `data/log/`：与 backend 无关的 FD 日志 connector；
- `runhost/appuid/AppUidRunHostService`：App UID + app SELinux domain的thin framework entry，本地保留`Service/onBind/stopSelf`；
- `runhost/shizukushell/ShizukuShellRunHostService`：Shizuku shell UID 2000 + `u:r:shell:s0`的thin framework entry，本地保留constructors/live context/`System.exit(0)`。

两个 framework entry 复用同一 `app/src/main/aidl/com/ghostlock/app/run/` 合同，各自只调用一次`BackendRegistry.runtime(key)`取得backend-owned runtime projection，不得读UI catalog、definition list或建第二份registry。六个business override只委托同一endpoint/controller路径；第七个framework `destroy()`仍由entry本地查询controller授权后分别执行`stopSelf()`或`System.exit(0)`。

`RunHostController`是lifecycle、candidate、callback/snapshot、cancellation、Flow worker、log sequence/drop与唯一`inFlightPhysicalOperations + currentEpochCompletion`的owner。materializer、worker、safe stop和workspace cleanup登记同一epoch，所有物理调用与barrier等待都在controller mutex外；因此RUNNING未claim时close立即返回`UNSAFE_TO_DESTROY`，其余允许close的状态不得越过最后访问者。endpoint不拥有framework destroy，preparation adapter不拥有lifecycle/callback/snapshot。

application侧不再有混合的`AndroidRunSessionAdapter`：coordinator在同一次admission中固定executor与actor cutoff，dispatch adapter持有request descriptor lease；LogHub拒绝或pre-commit失败由application关闭lease，accepted commit后由execute closure终结。PRODUCT_ROOT的Flow只看到neutral `RootScriptResultReader`，不会接触`File`、descriptor或`RootScriptResultCodec`的physical入口。

`P-RUN-X-001/批次03`只收敛上述host orchestration；AIDL method/field/integer、既有state-machine转移、Payload/Finish、JNA ABI、client与产品成功语义保持冻结。
