# KMP UI与全量图标后续需求

## Context and scope

完整迁入的有效设计/范围与验收正文；不是新增实施批。原批准/历史结果按原Beads记录，工具工件齐备不代表实现或门禁通过。

来源：`043fdf6a:.beads/issues.jsonl`；保留语义代号和原锚点。本文中的历史进度/门结果文字仅是其具名输入的沿革，当前状态从proposal列出的Beads读取。旧机制由正文已注明的后继差异取代；基线不重新授权实施。

## Decisions

<a id="record-gl-75t"></a>

## P-UI-CORE-001：按最终 plugin owner 细化 KMP UI

<!-- migrated-source: 043fdf6a Beads gl-75t description -->
本节是迁入OpenSpec的现行设计/验收正文；Beads只保存执行状态、原批准和实际证据。历史来源仅作追溯。
设计待重开：最终plugin owner形成后，冻结app/shared、唯一I18nRegistry、platform shells与feature/presentation；不自动批准Desktop产品或生产实现。
原始来源（已归档/承接）：docs/archive/20261121-1301-backend-root-execution-unification-plan.md#p-ui-core-001

<!-- migrated-source: design gl-75t -->
<!-- source: f882e670:docs/plan/backend-root-execution-unification-plan.md:L4908-L4967 -->
<a id="p-ui-core-001"></a>

###### P-UI-CORE-001/批次01：App KMP shared UI、平台壳与 feature/package 收口（远期目标已记录，exact design待重开）

> 状态（2026-11-18）：用户明确补充“PC可能共享同一套UI”，因此当前纯Android `:app`不能再作为全工程终态。本节只记录后继批的目标结构、当前事实与硬边界；尚未形成逐文件scope、Gradle exact配置、desktop产品承诺或用户production批准。PC/shared-App目标本身不混入当前plugin实现；唯一KMP `I18nRegistry`、共用catalog schema、authenticated package bundle与Host declarative renderer继续作为决策03保持项，但旧`P-BACKEND-X-002/批次01` 240行scope已随runtime替代整体失效，不能直接实施。等决策03获认可并由首个plugin vertical形成真实owner后，本批按L级重新Explore／Design、作者自检、非作者critic与用户批准；未发生的compile／desktop run／UI目视门均`NOT_RUN`。

整体app子系统目标采用一个共享KMP UI／presentation模块和两个独立entry shell；`app/`只作为组织根，不再同时冒充Android leaf project：

~~~text
app/                                             # application subsystem root; no build.gradle.kts in target state
  shared/                                       # Gradle :app:shared; KMP Android + JVM library
    build.gradle.kts                            # Kotlin Multiplatform + Android-KMP library + Compose Multiplatform
    src/commonMain/kotlin/com/ghostlock/app/
      presentation/                             # platform-neutral state, intents, presenter/store contracts
      ui/                                       # shared App root, navigation, screens, components, Material/Miuix themes
      platform/                                 # narrow ports only; no Android Context/Activity/PFD/Shizuku
    src/commonMain/composeResources/
      files/i18n/app/en.json                    # app builtin bundle; same schema/resolver as plugin catalogs
      files/i18n/app/zh.json                    # scriptless Chinese fallback; add Hans/Hant only with real translation
      drawable/                                 # icons/images remain ordinary Compose Multiplatform resources
    src/androidMain/kotlin/                     # Android-only UI and locale-environment adapters
    src/jvmMain/kotlin/                         # desktop-only UI and locale-environment adapters; no entrypoint here
    src/commonTest/kotlin/
    src/jvmTest/kotlin/
  android/                                      # Gradle :app:android; com.android.application
    build.gradle.kts
    src/main/AndroidManifest.xml
    src/main/aidl/                              # Binder/RunHost wire remains Android-owned
    src/main/kotlin/com/ghostlock/app/android/
      GhostLockAndroidApplication.kt
      MainActivity.kt                           # only calls shared GhostLockApp composable with Android adapters
      composition/                              # Android DataStore, SAF/URI, Shizuku, RunHost, plugin install/load
  desktop/                                      # Gradle :app:desktop; future Kotlin/JVM Compose application
    build.gradle.kts
    src/main/kotlin/com/ghostlock/app/desktop/
      Main.kt                                   # Window/application entry; calls the same shared GhostLockApp
      composition/                              # desktop filesystem, chooser, process/plugin-host adapters
~~~

`:app:shared` targets=`android`与`jvm`，依赖`:plugin:core`的对应target，但`commonMain`不得依赖Java-only`:plugin:api`、Android Host implementation、AIDL、JNA或filesystem。`:app:android`消费`:app:shared` Android target，并继续拥有APK、Activity/Application、Service、AIDL、DataStore、SAF、Shizuku、RunHost、Android plugin verifier／installer／classloader及平台resource adapter；`:app:desktop`消费`:app:shared` JVM target，并在真实PC批次拥有JVM `main()`／Window和desktop adapters。官方[推荐KMP模块结构](https://kotlinlang.org/docs/multiplatform/multiplatform-project-recommended-structure.html)与[Compose平台入口说明](https://kotlinlang.org/docs/multiplatform/compose-platform-specifics.html)要求各平台有独立entry point并从共同代码调用主Composable；新[Android-KMP plugin](https://developer.android.com/kotlin/multiplatform/plugin)只适用于library而不能替代`com.android.application`，所以共享UI与Android APK shell必须是两个leaf project，而不是把Activity塞进`commonMain`。

App可见文案不再建立一个与plugin并行的静态资源authority。`:app:shared`通过[Compose Multiplatform resources](https://kotlinlang.org/docs/multiplatform/compose-multiplatform-resources-usage.html)携带`files/i18n/app/*.json`的raw bytes，但它们在启动时以保留typed identity `HOST_APP`注册进§19.5.47.1的唯一`I18nRegistry`；common UI与plugin settings均使用`I18nTextRef(bundleIdentity,key,args)`与同一`LocalI18n`，不保存Android `R.string`整数，也不直接以生成的`Res.string`取文案。`Res`继续处理图标、字体与catalog raw files；Android壳的`res/`只保留Manifest label、Service／Notification等Android framework必须按resource ID读取的最小平台壳文案，这些platform-only文案不能被shared UI或plugin引用。

`:app:shared`只提供`LocalI18n`的Compose adapter：`SYSTEM`跟随platform adapter提供的ordered locale list，或把用户显式选择的canonical BCP-47 tag交给唯一`I18nRegistry`；不再拥有第二locale state。Android和Desktop只实现系统locale读取／变更通知与持久化adapter，不各自维护第二language selection。locale改变或plugin bundle generation变化必须触发shared UI与当前plugin settings surface一起recompose，不要求重启或重新安装plugin。

“同一套UI”冻结为同一个`commonMain` composable tree、navigation/presentation state、Material/Miuix theme、字符串／图标资源和用户action合同；不是复制一份desktop screens，也不是要求两个平台具有相同系统能力。Android的root执行、Shizuku、SAF、安装权限和真实设备状态由`:app:android`实现窄port；desktop的文件选择、filesystem、process／toolchain与窗口行为由`:app:desktop`实现。shared UI按Host capability/readiness渲染可用、不可用或未实现状态，不散布`if (desktop)`并且不让PC伪装能运行Android-only backend。

current source侦查基线为`app/src/main/kotlin/com/ghostlock/app/ui`共133个Kotlin文件，其中至少35个直接引用Android／Activity／Context／`LocalContext`／Lifecycle等平台面，故不能机械整目录搬入`commonMain`；须逐真实caller把纯Compose UI、state与action移入shared，把系统调用留在platform adapter。资源侧目前只有Android `values/strings.xml`与`values-zh-rCN/strings.xml`，build又把locale filter固定为`en`／`zh-rCN`；默认catalog约793项、中文约418项，按name机械比较有375个default key没有中文对应，41个UI Kotlin文件仍直接引用Android `R.string`／`androidx.compose.ui.res.stringResource`。这些数字只是迁移基线，不把缺少翻译本身冒充代码错误；exact P-UI批须验证default key完整、已提供翻译的placeholder/plural签名、unsupported locale fallback、`zh`／`zh-Hans`／`zh-Hant`和RTL布局，并把Android resource ID从shared presentation model清零。current Miuix 0.9.4的`miuix-ui`、`miuix-nav`、`miuix-icons`、`miuix-preference`与`miuix-blur` Gradle metadata均发布desktop variant，证明共用视觉栈有可行入口，但不替代实际desktop compile、交互与目视证据。

原有UI收口需求继续作为该批输入而不是独立并行迁移：

- 删除经最终复扫仍无包外 caller 的 `ui/component/profile`、`rebootlistpopup`、`miuix/effect`；这项新用户要求取代旧 D7 “所有 component 文件一律保留”的历史 fixture 结论；
- `ui/screen/module` 归为 Backend/Payload/Finish feature；Screen、presentation state与action按feature共置或以中立model单向连接，消除 `ui.screen <-> ui.viewmodel`及Android lifecycle类型进入common；
- `BackendStatusTone` 移入中立status-card/UI model owner，消除component对backend UI的反向依赖；UI kit可以依赖component，component不反向依赖kit/backend；
- current `BackendUi.categoryLabelRes/displayNameRes`、`BackendUiCatalog.text(Int)`、`BackendStatusCard`等`@StringRes Int` presentation合同统一改为`I18nTextRef`；App文案用保留的`HOST_APP`，plugin文案用从authenticated `publisherId+packageId`派生的`PLUGIN(...)`，两者经同一registry／active locale／format engine解析，但不按同名key跨bundle fallback；
- current Android `Application`类`GhostLockApp`迁入`:app:android`并改名为`GhostLockAndroidApplication`；`:app:shared`以`GhostLockApp`作为唯一共享Composable根，由Android Activity与desktop `main()`共同调用，不再让Application与UI根同名。`AppContainer`拆为platform composition owner，Home、Run、Backend、Payload、Finish、Settings、ColorPalette不再各自`LocalContext -> GhostLockApp.container`；
- 最终结构门要求platform composition↔shared UI service-locator环为0；不新增DI framework、全局singleton或第二catalog。Material/Miuix外观、字符串、多语言、语义icon与页面路由保持，并分别执行shared common/JVM tests、Android UI fixture/目视及真实desktop window交互门；任何一端未运行不得写成共享UI已交付。

本结构不会让同一个`.glplugin.zip`代码payload自动跨平台：Android package仍可含DEX／Android DSO，desktop package以后按JVM JAR/classes与desktop native variant另行设计；PC首批可以只消费Document、extractor、toolchain或data provider。共享app UI只复用presentation，不取消plugin package的Host runtime／ABI选择。



<a id="acceptance-gl-75t"></a>

### 验收定义与原边界：gl-75t

<!-- migrated-source: acceptance_criteria gl-75t -->
设计待重开：最终plugin owner形成后，冻结app/shared、唯一I18nRegistry、platform shells与feature/presentation；不自动批准Desktop产品或生产实现。

批准/审核/实际结果：Beads `gl-75t` notes；本段不维护实时勾选或新PASS。

<a id="record-gl-72l"></a>

## P-UI-ICON-002：全量图标目录设计

<!-- migrated-source: 043fdf6a Beads gl-72l description -->
本节是迁入OpenSpec的现行设计/验收正文；Beads只保存执行状态、原批准和实际证据。历史来源仅作追溯。
REQUIREMENT RECORDED / DESIGN PENDING：输入style/weight、并集配对/fallback、升级生成、APK/编译成本按原需求设计；不混入已完成ICON001，不阻断B1。
原始来源（已归档/承接）：docs/archive/20261121-1301-backend-root-execution-unification-plan.md#p-ui-icon-002

<!-- migrated-source: design gl-72l -->
<!-- source: f882e670:docs/plan/backend-root-execution-unification-plan.md:L656-L663 -->
<a id="p-ui-icon-002"></a>

##### P-UI-ICON-002/需求01 Material/Miuix 全量图标目录（开发者需求已登记；设计待定）

2026-11-08开发者明确要求目录最终覆盖Material与Miuix当前依赖版本导出的每一个icon，而不是只覆盖项目current callsite。当前依赖静态证据为Material Extended 1.7.8共11,105个Kotlin图标源/类（五个普通style各2,083，加690个AutoMirrored），Miuix Icons 0.9.4共156个图标名、每个extended icon通常有default及Light/Normal/Regular/Medium/Demibold六个入口；对应上游Android archive约34 MiB与1.8 MiB。

该需求不回滚已批准的001语义入口和current caller迁移，但取代“项目目录永久只收录current usage”的长期结论。002须在单独设计中冻结style/weight命名、Material与Miuix并集配对、单侧fallback、生成输入权威、依赖升级重生成、R8可达性/APK体积与编译时间判据；在这些合同和逐文件scope获批前，002只作为后续需求，不把全集引用夹入001 source。



<a id="acceptance-gl-72l"></a>

### 验收定义与原边界：gl-72l

<!-- migrated-source: acceptance_criteria gl-72l -->
REQUIREMENT RECORDED / DESIGN PENDING：输入style/weight、并集配对/fallback、升级生成、APK/编译成本按原需求设计；不混入已完成ICON001，不阻断B1。

批准/审核/实际结果：Beads `gl-72l` notes；本段不维护实时勾选或新PASS。
