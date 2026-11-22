# 插件决策03共享架构

## Context and scope

完整迁入的有效设计/范围与验收正文；不是新增实施批。原批准/历史结果按原Beads记录，工具工件齐备不代表实现或门禁通过。

来源：`043fdf6a:tools/engineering/.beads/issues.jsonl`；保留语义代号和原锚点。本文中的历史进度/门结果文字仅是其具名输入的沿革，当前状态从proposal列出的Beads读取。旧机制由正文已注明的后继差异取代；基线不重新授权实施。

## Decisions

<a id="record-gl-sfg"></a>

## P-BACKEND-X-002/决策03：分平面插件与真实消费者

<!-- migrated-source: 043fdf6a Beads gl-sfg description -->
2026-11-21迁入的共享设计记录，不是新的执行任务。closed仅表示该记录收录完毕，不表示业务实现、新批准或门禁通过。
父设计独立审查和用户认可完成，不代表所有后继production获准。旧决策01/02、69/240行方案已取代；B1是独立获批exact增量。

<!-- migrated-source: design gl-sfg -->
<!-- source: f882e670:docs/plan/backend-root-execution-unification-plan.md:L4225-L4407 -->
<a id="p-backend-x-002-decision-03"></a>

###### P-BACKEND-X-002/决策03：分平面插件体系、普通依赖视图与特权 Custom 能力目录（L级替代设计；独立审查及用户认可完成）

> 状态（2026-11-20）：本节响应用户对“完全体仍然过度设计”的复核，以及Custom必须能够枚举并调用任意已加载插件公开能力、直接管理native内存或向指定地址写入的新增硬需求。它在production实施前整体取代决策02、旧资源准备候选和旧批次01 exact design；保留`.glplugin.zip`组包、独立provider、plugin-owned native、builtin/imported同路、thin variant、KMP、统一i18n与declarative settings等已确认目标，但不继承旧的通用`ComponentContext`、Diagnose自用service registry、Custom filtered dependency或240行实施账本。本节只冻结职责、调用方向、危险边界与后继设计入口，不冻结public method、manifest字段、wire、class名、storage schema、进程数或逐文件scope；作者自检与非作者critic均已完成，critic总判可进入用户认可gate且阻断／重要／建议均为0。用户已在该结论后明确回复“认可”，故`BXP3-D3`关闭并开放首个vertical的exact design；该认可仍不等于production批准，实施和全部产品门均未完成或`NOT_RUN`。2026-11-20的封装纠正只把当前格式明确为普通ZIP可编辑载体，不改变决策03职责平面。

###### 19.5.58 需求基线与本次优化判据

本次不再从“一个万能registry能否容纳所有未来功能”反推框架，而从已经具名的consumer反推最小稳定边界。下列需求是决策03的硬输入：

| 需求／痛点 | 决策03必须保留的结果 | 不再接受的替代 |
|---|---|---|
| 上游只更新设备profile／配置而不更新攻击代码时，用户不应重装APK | profile／配置作为可独立安装、更新、回滚的data provider输出；backend消费canonical Document | 配置继续编进APK或backend私有parser；只为数据更新重发backend／App |
| 43284需要预建KO，用户也可能导入自定义KO | 预建、导入或toolchain build均产生同一typed Artifact；来源和provenance保留 | KO固定塞进backend源码树；backend自行扫描文件或provider实现 |
| extractor、toolchain、importer、exporter需跨backend／PC复用 | 它们是provider package执行的有限job，输入显式source／参数，输出immutable Document／Artifact | 为每种动作增加新的安装系统、component kind或常驻service |
| backend实现及其任何私有组件／动态库可以独立分发 | code-bearing package包含Kotlin／Java、C/C++源码构建产物、私有依赖和数据；插件Kotlin拥有DSO load／call／close | Host持有backend native、JNA mapping、handle或按symbol替backend调用 |
| 一个分发文件可同时包含backend和紧耦合provider | `.glplugin.zip`是明确可由普通ZIP工具编辑的原子签名／版本／安装单位，内部可有多个component | 隐藏ZIP载体、再叠加`PluginGroup`或要求一component一archive |
| builtin与外部导入行为一致 | 同一verifier／installer／catalog／resolver／RunHost loader；只在provenance、trust admission、seed和update policy上有差异 | builtin静态registry旁路、外部包另一套runtime |
| Android ARM64、ARMv7、x86及以后PC不能让单个安装包无限膨胀 | 同一逻辑release发布按实际RunHost ABI划分的thin archive；纯数据可universal | 把所有ABI DSO和所有KMI KO无条件塞入每台设备 |
| App与plugin在Android／Desktop共用UI与语言资源逻辑 | KMP shared UI、唯一locale／catalog resolver与Host declarative renderer | Android `R.string`成为跨平台／plugin ABI，或执行plugin自带`@Composable` |
| 普通backend只应得到已声明依赖，但Custom要调用任意已加载插件的公开能力 | 普通依赖视图与Custom能力目录分开；二者来自同一authenticated catalog／export声明 | 把Custom伪装成普通filtered consumer，或让普通backend枚举全局对象 |
| Custom需深入到底层管理native内存、指定绝对地址读写 | plugin可显式导出Custom-callable raw-memory／operation contract；Custom可枚举并调用 | 任意JVM反射、`Class<?>`／constructor、JNA `Pointer`／`Native.invoke`或私有对象泄漏 |

“公开能力”指插件作者在authenticated metadata与shared API中明确导出的稳定contract，不指该classloader内所有public JVM member。插件native与Custom本来就运行在高权限执行域；registry用于身份、兼容性、发现、调用与生命周期治理，不宣称能把同进程恶意code变成安全沙箱。

###### 19.5.59 一个目录权威、五个职责平面、三种执行context

决策03保留一个Host维护的plugin catalog作为installed membership／exact object／provenance／enabled状态的唯一权威；“五个平面”只是对同一事实在不同生命周期的职责投影，不是五个Gradle module、五个数据库、五个进程或五套registry：

1. **package／catalog plane**：验证、immutable install、builtin seed、版本／variant选择、声明索引和exact identity；不执行plugin code。
2. **presentation／i18n plane**：读取authenticated declarative settings／information tree与语言catalog，由Host KMP UI渲染；不执行plugin Compose或以UI对象传依赖。
3. **preparation／artifact plane**：Host拥有revisioned draft与调度，provider执行有限job并发布immutable Document／Artifact；不建立可枚举live service registry。
4. **ordinary backend run plane**：backend消费本次run冻结输入及自己声明的少量跨package依赖；同package私有实现直接调用，不为证明框架而注册。
5. **privileged Custom plane**：专用、可丢弃RunHost在session开始时建立可枚举目录，暴露该session已加载package明确标为Custom-callable的公开能力。

~~~mermaid
flowchart LR
  PKG["Authenticated package catalog<br/>唯一身份／安装权威"]
  UI["Presentation + I18n<br/>Host只读声明并渲染"]
  PREP["Preparation jobs<br/>Document／Artifact"]
  RUN["Ordinary backend run<br/>frozen input + declared lookup"]
  CUSTOM["Dedicated Custom session<br/>enumerable public capability directory"]
  OUT["Run result／published artifact"]

  PKG --> UI
  PKG --> PREP
  PKG --> RUN
  PKG --> CUSTOM
  PREP -->|immutable identity + digest| RUN
  RUN --> OUT
  CUSTOM --> OUT
~~~

真正传给plugin code的公开执行context只按三种生命周期设计，取消旧批次把所有职责塞入一个`ComponentContext`的做法：

| context概念 | Host提供 | 明确不提供 |
|---|---|---|
| preparation job context | 已批准input handle／参数、取消、日志、临时workspace与原子output publisher | backend run状态、全局registry枚举、其它plugin私有对象、长期singleton |
| ordinary backend run context | frozen Document／Artifact、run metadata、cancellation／log／result adapter、manifest已绑定的dependency lookup | provider选择、动态UI draft、全局枚举、运行中换版／换artifact |
| Custom execution context | frozen session facts、Host基础服务和可枚举Custom capability directory | main UI process对象、任意reflection／classloader入口、未声明私有export、跨session活动handle |

这些名称描述职责而非冻结API类名；exact batch可以在不混合三种生命周期的前提下合并纯DTO或拆分platform adapter。Host不是“只搬运”：它拥有身份验证、依赖解析、draft revision、调度、输入输出边界、freeze、取消与终态；Host也不解释某个profile字段、KO构建规则或native地址的领域含义，这些由对应plugin contract负责。

###### 19.5.60 package、component、export与可见性

安装模型仍只有`PluginPackage`。一个package可以原子包含多个component；决策03只保留三个有真实调用差异的角色：

- `BACKEND`：一次run的产品／诊断编排根，消费冻结输入并产生typed result；
- `PROVIDER`：供应数据或执行import／extract／build／export等准备工作；静态数据provider可以没有entry code；
- `CUSTOM`：用户自定义程序／脚本的执行根，使用特权能力目录；它不是拥有特殊安装规则的第二包格式。

`SUPPLY`、`IMPORT`、`EXTRACT`、`BUILD`、`EXPORT`是provider job语义，不再作为component kind。跨component输出按生命周期只分三类；exact type名和method留后继批：

| export类别 | 形态 | 典型例子 | 生命周期 |
|---|---|---|---|
| value | immutable Document／Artifact／bounded metadata | profile、解析后的配置、预建KO、build产物、动态选项数据 | content identity决定；可跨process／跨run引用 |
| job | 显式输入→有限执行→一次发布或失败 | conf/json导入、extract、toolchain build、Document反向序列化 | 单次preparation operation；不作为常驻对象 |
| service | typed live capability／factory | 跨package shared primitive、Custom raw-memory／operation capability | ordinary run或Custom session内；不得跨session缓存 |

每个export再有一个正交可见性：

- `PRIVATE`：只供同package实现直接调用，不进入Host runtime directory；例如Diagnose backend直接构造同包Kotlin binding。
- `DEPENDENCY`：只向manifest已解析并绑定该slot的ordinary consumer开放；不可枚举全局能力。
- `CUSTOM_CALLABLE`：进入特权Custom session目录，可由Custom枚举和调用；是否也可作ordinary dependency由声明分别决定，不能从一种可见性暗推另一种。

同一catalog声明、exact package identity和shared contract是三种可见性的共同来源；Host不会维护“普通插件表”“Custom operation表”“builtin静态表”三份membership。package内私有调用无需绕registry，跨package依赖和Custom公开调用则必须通过各自受限视图，避免一张`Map<String,Any>`同时承担安装、服务定位和脚本反射。

###### 19.5.61 preparation／Document／动态选项

资源准备固定为“Host编排状态，provider执行领域job，backend只消费结果”。它与ordinary run registry脱钩，流程为：

1. Host从authenticated声明建立backend requirement、兼容provider候选及declarative presentation；没有选择或存在歧义时保持`NOT_READY`，不按catalog顺序静默选第一个。
2. 用户选择源文件、自定义KO、目标设备或构建参数后，Host生成新draft revision与输入fingerprint；presentation字段属于draft数据，不成为另一类plugin/component。
3. Host在适当的preparation execution domain调用provider job。provider可以使用本package私有Kotlin／native／toolchain，但只能取得该job获准的input handle与参数，不能扫描全局registry再自行拼pipeline。
4. importer可把硬盘conf/json直接反序列化为canonical Document；extractor也可直接产出Document，不要求先生成中转配置文件；toolchain产出Artifact；exporter从Document生成用户选择的外部格式。
5. Host校验output envelope、size／identity／digest／provenance并原子发布immutable结果。旧revision迟到结果丢弃；依赖输入变化只使受影响下游结果失效。
6. 用户提交run时，Host只把accepted revision中的exact provider／variant／Document／Artifact identity冻结进run input；accepted run不轮询registry、不热换provider、option或artifact。

动态选项不需要把“UI配置项”提升成provider管线中间component。backend／provider声明稳定field key、类型、依赖和文案；Host拥有当前value、validation、visibility和revision。若候选值必须通过探测、extract或toolchain计算，provider job返回bounded纯数据，Host将其应用到同一draft并产生新revision；不把provider JVM对象、callback或Compose节点注入UI。简单显示规则由声明求值，复杂领域求值等真实consumer出现时再冻结DTO，不预建通用reactive workflow engine。

provider之间若确有输入输出关系，由Host依据signed value／job requirement形成有限DAG并传递immutable结果；provider不得互相枚举或轮询。首版只需要直接依赖、cycle／missing／ambiguity拒绝和用户pin，不引入SAT solver、全局最优版本求解或隐式fallback。

###### 19.5.62 ordinary backend run与plugin-owned native

ordinary backend run只处理已经准备并冻结的事实：

1. Host按selected backend、execution domain、RunHost ABI、exact package digest与input identities构造run plan；main UI process不加载plugin code。
2. 目标RunHost装载backend package。backend从ordinary run context取得immutable value和已声明的跨package dependency；不能枚举未绑定plugin，也不能在运行中发起安装、换版或资源准备。
3. 同package的helper、binding、native primitive和flow step直接由backend实现调用。只有出现真实跨package consumer时，才把某能力声明为`DEPENDENCY` service；不再以“Diagnose把自己的`DiagnoseNativeBinding`注册后再查回来”证明registry纵向。
4. 任何DSO都由所属plugin的Kotlin／Java entry在自己的classloader／native namespace中load、call、close；Host只建立RunHost与受控文件／FD边界，不声明native method、不持有JNA mapping／pointer／handle，也不按symbol替plugin调用。
5. 正常终态按backend私有对象和实际跨package service lease的逆依赖顺序关闭。通用registry不替真实攻击primitive宣称安全回滚；一旦primitive可能改变kernel／外部状态，成功、取消、失败与process death都必须保留真实held-state／unknown-state语义。

Host可在shared API中提供log、cancel、artifact access、typed result等稳定基础服务，Custom也可调用公开的Host operation；但current Host-owned `Flow`／`SequentialFlow`和plugin-private `NativeBinding`实现类都不直接跨classloader注册。需要被Custom复用的“flow调用能力”应提升为带稳定输入／输出／取消语义的public operation contract，由原owner提供adapter；需要被Custom复用的native能力由plugin导出semantic primitive，native owner仍不转移给Host。

###### 19.5.63 privileged Custom能力目录与raw memory边界

Custom不再是带一组预声明filtered dependency的普通backend。每次Custom执行建立独立、可丢弃的RunHost process和冻结session：Host先从当前enabled、trusted、平台兼容且允许进入该execution domain的对象中确定本session exact package集合，解析每个公开export自身声明的依赖闭包，再装载其中声明为`CUSTOM_CALLABLE`的export并向Custom提供只读可枚举目录；“可枚举”不会让provider跳过它自己的required dependency。目录至少能返回export identity、provider package／component、contract与API version、target／scope、操作描述及创建typed handle的入口；Custom可以调用该session中任意匹配公开contract的能力，不必在自己的manifest逐项列dependency。不是所有installed package都必须无条件装入一个session；exact选择policy留Custom批根据启动成本与真实UI决定。安装／启用／升级plugin或改变package集合只影响下一session，不在活动session中热插拔。

“一步深入最底层”通过明确的raw-memory与operation contract实现，而不是反射Host／plugin私有对象。决策03冻结以下语义能力，exact接口与数值边界留真实Custom批：

- 指定address space与target identity，按绝对address执行bounded read／write；支持用户明确给出要写入的地址和bytes／scalar。
- 分配、查询和释放由该capability创建的owned native region；owned allocation与外部／kernel任意地址必须是不同handle／result语义，禁止把“能写”误写成“Host拥有并可回收”。
- 暴露必要的protection、alignment、endianness、width、partial transfer与native error事实，不让Host猜测底层ABI或吞掉provider结果。
- plugin Kotlin／private DSO拥有真实process／device／kernel handle、mapping和cleanup；Custom持有shared contract handle，Host只管理session引用与终结顺序，不接管native实现。
- compiled Custom使用typed shared contracts；Lua或以后其它脚本只经Host维护的descriptor/codec bridge调用同一operations，不获得JNA `Pointer`、`Memory`、`Library`、`Native.invoke`、任意class name／constructor或JVM object conversion。

这条边界允许绝对地址写入，但不虚构可逆性：外部process、device或kernel memory写入一旦发生，关闭handle或销毁Custom process不等于回滚；任意写、并发primitive与部分成功必须进入run report／journal所需事实，终态可为unknown held-state。专用process的意义是隔离Host UI、classloader污染和活动handle，不是阻止已获高权限的plugin／Custom影响目标系统。普通backend和preparation job永远拿不到Custom的可枚举目录。

###### 19.5.64 依赖调用、owner与终结矩阵

同一Host registry维护package声明与export identity，各平面只取得与其职责相符的运行视图：

| 场景 | 谁选择／解析 | 谁调用 | 谁拥有资源／close | 失败后事实 |
|---|---|---|---|---|
| 静态profile／预建KO | Host按requirement、compatibility与用户pin | 无plugin code或Host读取value | immutable catalog object／artifact lease | 缺失、歧义、digest不符即`NOT_READY` |
| importer／extractor／toolchain／exporter | Host preparation scheduler | Host调用provider job | provider拥有私有资源，Host拥有workspace／output publish | job失败不发布；旧revision结果不得进入新draft |
| backend同包native | package实现决定私有调用 | backend Kotlin直接调用binding | plugin binding／DSO | backend按真实结果收口；Host不第二次close native handle |
| backend跨包service | Host按signed dependency slot绑定 | backend经non-enumerable dependency lookup | provider service handle；RunHost治理lease终结 | missing／wrong API在code load前拒绝；活动run不重解 |
| Custom公开capability | Host冻结session package set并建目录 | Custom枚举后调用typed handle | export provider拥有实现；Custom RunHost治理session | session销毁关闭handle，但不声称撤销外部／kernel副作用 |
| Host flow／log／artifact基础服务 | Host按execution context提供 | backend／Custom调用shared adapter | Host | 只治理Host资源；不成为plugin native owner |

依赖关系与调用入口因此仍“由Host注册表调用”，但不是所有对象都经过同一种lookup：ordinary依赖必须声明、绑定且不可枚举；Custom只能枚举显式`CUSTOM_CALLABLE`；package私有实现不注册。这样既保留Custom调用任意已加载公开能力，也避免普通backend和preparation pipeline退化成全局service locator。

###### 19.5.65 builtin/imported、KMP、i18n与多架构保持项

决策03不重开以下已确认方向：

- builtin archive由APK／desktop distribution携带，只是`BUILTIN_SEED`来源；首次可用启动由唯一seeder调用同一verifier／immutable installer写入同一catalog，崩溃后幂等恢复，catalog未`READY`不得selection／run。imported package走相同identity、compatibility、resolver和RunHost loader；差异只允许存在于初始trust、能否禁用／卸载、update channel和seed reconciliation policy。
- Android code-bearing package仍需要受控dynamic code loading：只从app-private、immutable、已验证且适配当前variant的installed object建立package classloader／native search path，shared API由Host parent提供，plugin依赖和DSO留在package closure。DCL只是把exact code装入目标RunHost的机制，不兼任capability registry或native owner；exact classloader类型、只读时序与API 34／37处理在新的implementation batch按当时minSdk／targetSdk核对。
- `plugin/api/`保存最小跨classloader binary contracts，`plugin/core/`保存Android/JVM可共享的catalog／Document／resolver／presentation／i18n模型，`plugin/packages/<id>/`保存实际package的Kotlin／Java及C/C++源码；`plugin/native-source/common/`只可作为被具体package编译进去的source authority，不产Host／公共DSO。exact Gradle plugin、source set和task在新的implementation batch再核对，不继承旧240行ledger。
- `.so`、源码构建`.ko`、DEX和`.glplugin.zip`都是leaf package build output；不得放入production source目录冒充源码。预建KO是带provenance的provider input／artifact，自定义KO是importer输入；Host/App不因打包它们而成为native owner。
- 一个逻辑package release可发布`android-arm64-v8a`、`armeabi-v7a`、`x86`、`x86_64`或以后desktop等thin archive；设备只安装实际RunHost ABI所需variant。target ISA／KMI属于Document／Artifact事实，不等于Host ABI；纯数据package可为`universal`。

<a id="retained-i18n-goal"></a>

- App终态仍是`:app:shared` KMP Compose UI加`:app:android`／`:app:desktop`平台壳。App base catalog、builtin plugin和imported plugin继续进入唯一`I18nRegistry`与locale/message engine；plugin只携带authenticated declarative presentation和语言data，Host渲染Material／Miuix Compose。既有i18n4k薄门面可作为后继exact batch的复用候选，不因本次runtime重画而另造message engine；旧exact版本／ledger不自动成为实施授权。
- 当前`docs/development/full-process-uml.md`描述已实现产品链，不为尚未实现的候选改画。决策03先在本节用候选图表达，待implementation切换真实owner时同批更新权威UML。

###### 19.5.66 明确删除的过度设计与暂缓项

| 决策02／旧批次机制 | 决策03处理 | 原因 |
|---|---|---|
| 一个public `ComponentContext`同时服务provider注册、backend run和Custom | 删除，改为三种生命周期context | 权限、可变性、失败与close语义不同；万能context会泄漏能力 |
| declaration index、run graph之后再用一套通用runtime registry承载所有value／job／service | 保留唯一catalog与解析事实，但按preparation、ordinary、Custom投影不同view | 保持单一authority，不强迫不同生命周期使用同一对象协议 |
| Diagnose native binding注册为service再由同包Diagnose查回 | 删除，package内直接调用 | 没有跨package consumer，只是在为框架制造需求和双owner风险 |
| future Custom必须预声明每个dependency并取得filtered lookup | 删除；Custom session枚举所有已加载`CUSTOM_CALLABLE` export | 与Custom的探索／底层控制目标冲突 |
| 把具体`NativeBinding`、Host Flow类或任意JVM对象注册给Custom | 改为stable operation／raw-memory contract与owner adapter | 保留能力，不把私有实现、JNA类型和classloader细节变成ABI |
| provider通过同一registry实时链式调用、轮询或注入UI | 删除；Host调度有限job并传immutable value | 资源准备需要revision／artifact语义，不需要常驻service locator |
| UI field成为provider或pipeline node | 删除；field是Host draft中的declarative data | 避免为动态选项建立通用reactive workflow引擎 |
| 批次01一次冻结完整Java ABI、manifest、wire、DCL、process终结、KMP、i18n和240行scope | 整体作废；决策03认可后按第一个真实vertical重新做exact design | 旧scope在真实Custom contract出现前过早冻结且已与需求冲突 |
| 为所有未来backend预建durable journal、SAT solver、OCI/TUF、远程repo、四ABI与PC runtime | 保留触发条件，不进入首个exact batch | 没有当前consumer；未实现不阻断当前痛点 |
| 以大量额外验证证明尚未实现的远期能力 | 只验证当批声明、实际target和真实consumer | 验证不放宽，但不得用未来矩阵扩大当前批 |

这里的简化不是取消身份、签名、digest、API／ABI、private install、process isolation、close或真实设备门；这些在相应实现存在时仍是必要判据。被删除的是没有真实consumer的通用层、重复视图、过早exact字段和把未来功能当当前准出条件。

###### 19.5.67 后继批次边界与实时checkbox

决策03经独立critic与用户认可后，当前只开放第一个exact implementation batch的设计。该批从真实Diagnose vertical选择最少能力且不复用旧240行ledger：先建立package/catalog＋builtin Diagnose私有native直调，随后才以data-only profile provider证明“上游更新配置无需重装APK”，再以真实Custom consumer冻结Custom directory与一个明确raw-memory／operation contract；若用户另行调整顺序，以当时真实痛点和caller为准。43499、43284／KO、98260、extractor、toolchain、Desktop和新增ABI各自在真正接入时补相称contract与门，不因框架能表达就记为implemented或supported。

- 原已完成步骤、批准与运行证据：见 Beads `gl-sfg` notes 的迁移前记录；本设计不维护进度checkbox。
- BXP3-B1：可编辑`.glplugin.zip`格式差异已完成作者自检及同一非作者critic定点复审，最终阻断／重要／建议／新增finding均为0；用户于2026-11-20明确批准本批production implementation，`BXP3-B1-P0`单变量preflight已通过，现进入本批production source，旧240行ledger保持历史、不可执行。
- BXP3-I1/V1：production已授权且P0通过；当前只开始本批148行ledger内的source implementation，architecture、完整unit、native、artifact、AVD与真机门仍`NOT_RUN`，Desktop不在本批范围。



<a id="acceptance-gl-sfg"></a>

### 验收定义与原边界：gl-sfg

<!-- migrated-source: acceptance_criteria gl-sfg -->
保留原文各行为判据、当轮证据和未关闭设备门。此decision收录完成不关闭任何关联业务任务。

批准/审核/实际结果：Beads `gl-sfg` notes；本段不维护实时勾选或新PASS。
