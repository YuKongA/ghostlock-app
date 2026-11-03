# 后端注册表统一（每个后端一个 object：UI card + flow 入口；单一注册表驱动 UI）

> 状态：**B1–B4 已交付（2026-11-02）**；G17 复核（阻断 0/重要 5）→ 实施完成，设备 UI 走查通过；B5 收口见文末。归属主计划 v5 / `debug-probe-backend.md`。
> 用户原话：「为什么BackendId没注册的后端在UI依旧能选？为什么BackendId里面绑定的类是flow impl而不是UI card？这代表UI是手动维护的，我要求：每个后端一个object实现某种接口，包含UI card，flow入口，然后唯一的注册表注册这个object，UI框架通过读取注册表生成后端菜单和选定后展示的后端配置项目ui card」。

## 1 现状与根因（file:line）

1. **`BackendId` 是死遗留**：`BackendId.kt:15-20` 枚举绑定的是 `Flow<*>` 实现类（`Flow43499Impl` 等），但全仓已无 `spec.id`/`BackendId.clazz` 的读者（B5c-1 删旧 runner 后失效）。它既不是选择权威，也不是 UI 卡来源。
2. **可选择性是手写白名单**：`BackendSpec.kt:239-241` `Backends.isSelectable` 用对象身份硬编码 `spec === Backend43499Glued || spec === Backend43284Glued`（另加 debug probe 分支）。所以“没在 BackendId 注册”的后端能不能选，取决于这里，而不是注册表/BackendId。
3. **配置卡是可选的旁路**：`BackendSpec.customConfig`（`:84`）可空；`BackendPage.kt:104-115/179-190` 有 `customConfig ?: rows()` 两套渲染分支。UI 已经部分数据驱动，但接口不强制“每个后端自带 UI card”，且 `rows()` 是第二个卡来源。
4. **UI 菜单手工维护部分**：菜单项来自 `Backends.all`，但 enabled/可选判断走 `isSelectable` 白名单；`BackendPage.kt:96/133-135`、`PathViewModel.kt:42`、`BackendSessionStore.kt:45-72` 都直接调用它。
5. **注册表本身**：`Backends.builtins`（`BackendSpec.kt:222-229`）手工列表 + `Backends.register/unregister` 动态项；没有统一不变量校验（唯一 key、selectable⇒有 flow 入口/有卡）。

## 2 目标架构

**每个后端一个 object，实现统一接口（UI card + flow 入口 + 参数/readiness 同在）：**

```kotlin
interface BackendSpec {
    val key: String
    @get:StringRes val displayNameRes: Int
    val selectable: Boolean                  // 唯一选择权威（debug probe = BuildConfig.DEBUG）
    val flowSymbol: FlowSymbol?              // flow 入口（反射 symbol；selectable ⇒ 非空）
    fun flowParams(context: Context): BackendFlowParams
    fun readiness(): BackendReadiness
    val uidRequirement: BackendUidRequirement
    val configSchema: BackendConfigSchema
    /** 选定后展示的配置卡（必经入口；内部可用 rows/默认卡）。 */
    @Composable fun ConfigCard(miuix: Boolean)
}
```

**单一注册表（唯一注册权威）：**

```kotlin
object Backends {
    private val builtins: List<BackendSpec> = listOf( ... ) // 每个后端一个 object
    var all: List<BackendSpec> by mutableStateOf(builtins)
    fun selectable(): List<BackendSpec> = all.filter { it.selectable }
    fun byKey(key: String): BackendSpec?
    fun isSelectable(spec: BackendSpec): Boolean = spec.selectable  // 删除身份白名单
    fun register(spec) / unregister(key) // 动态 Lua，校验唯一 key
}
```

**UI 框架（BackendPage Material/Miuix）**：
- 菜单：从 `Backends.all` 生成条目，`enabled = spec.selectable`（保留禁用项灰显，用户可见全部注册项）。
- 配置卡：`val spec = Backends.all[selected]; spec.ConfigCard(miuix)`——**只有一条渲染路径**，删除 `customConfig ?: rows()` 分支。
- 选择权威：`PathViewModel`/`BackendSessionStore`/`BackendRunGate` 只经 `BackendSpec.selectable`（或 `Backends.selectable()`），不再有对象身份判断。

## 3 迁移批次（单变量）

- **B1 接口/注册表**：`BackendSpec` 增必选 `ConfigCard`；`customConfig` 删除（或改名为 ConfigCard）；`rows()` 降级为 `DefaultBackendConfigCard` 的内部数据源；`BackendId.kt` 与 `id` 属性/**2 处** override（`BackendCve202643499.kt:23`、`BackendCve202643284.kt:39`；其余已删）删除；`Backends.isSelectable` 改为 `spec.selectable`；debug probe 的 `selectable` 保持 `BuildConfig.DEBUG`。
- **B2 迁移现有 object**：43499-Glued（现有 ProfileCard）、43284Glued（默认卡，rows 数据）、debug probe（默认卡，展示 env/log 说明）、被禁用的原版 43499/43284、Lua（现有 CustomLuaConfig）；`ConfigCard` 在接口给默认实现（`DefaultBackendConfigCard`，内部用 `rows()`），需要的 object 覆盖；**release 下 probe 从 `builtins` 过滤（菜单不出现）**，debug 才注册。
- **B3 UI/VM**：`BackendPage`（Material/Miuix）单一路径 `ConfigCard`；菜单/选择/持久化全部走注册表；删除 `customConfig` 分支与 `isSelectable` 手工调用（保留 UI 便利函数但实现=selectable）。`BackendSessionStore` 的 `cve.startsWith("$saved-")` 前缀兼容保留为**具名 legacy-key 迁移**（仅 exact key 缺失时；恢复后写回新 key，补测试），不得成为第二选择权威。
- **B4 测试/不变量**：注册表唯一 key；`selectable ⇒ flowSymbol != null`（`ConfigCard` 编译期强制）；**`Backends.register` 运行时 fail-closed 校验**：重复 key、`selectable && flowSymbol==null` 一律拒绝并返回 false（不静默注册）；debug probe 仅 DEBUG 且 release 不在 `all`；菜单条目数=注册数、可选项数=selectable 数；`PathViewModel/SessionStore/Gate` 仅经 `selectable`；删除 BackendId 相关测试/引用。
- **B5 设备/收口**：debug APK UI 走查（菜单 43499-Glued/43284Glued/DEBUG-PROBE；逐个选中显示对应卡；probe 可从 UI 跑并看到 log 落盘）；全量回归（B2/B4a/B4b/probe/Shizuku 取消/keepalive）；UML/计划/评审日志更新；删除 BackendId 文件。

## 4 门禁

- JVM 全量单测 + 构建（debug/androidTest/release）；`git diff --check`。
- 设备：A301SO debug APK 的 UI 走查（uiautomator dump 断言菜单/卡片标题；probe 运行 + persist 文件）；B2/B4a/B4b/G10/G11/G12 回归。
- 不变量测试必须“可失败”：临时复制一个缺 flowSymbol/缺唯一 key 的 spec 进 register 期望校验失败（可用测试内假 spec）。

## 4.1 迁移台账（G17 复核补充）

- 单 writer：同一 coding-agent 完成 B1–B3（接口/对象/UI 同批切，内部顺序：接口+默认卡+注册表→迁移 object→UI 单路径→删 BackendId/旧分支），B4 测试同批，B5 设备由 Lead。
- 工期：B1–B4 约 1.5–2 人日（agent 工时）；B5 设备 UI 走查 + 回归 0.5 人日。
- 备份：改动/删除前复制到 `/tmp/ghostlock-backend-registry-backup-<UTC>/` + `MANIFEST.sha256`；回退清单 = 修改文件列表 + 删除的 `BackendId.kt`。
- 双端 UI（Material/Miuix）同批改，无 feature flag 双轨。

## 5 风险/开放

- `ConfigCard` 把 Compose 依赖引入 backend 接口（已有 `customConfig` 先例，可接受）；动态 Lua spec 必须提供卡（Lua 当前已有 CustomLuaConfig）。
- 禁用 backend 是否仍进菜单：当前 UI 灰显保留；本设计保持（注册表全量、enabled=selectable）。
- `BackendId` 删除会影响旧测试/文档中的引用，需同批清理；`Flow<*>` 反射入口只保留 `flowSymbol` 字符串。
- 迁移期不得双轨：B1–B3 同批切换；旧 `customConfig/rows` 渲染分支在 B3 删除。


## 6 交付记录（2026-11-02）

- 接口：`BackendSpec` 增必选 `ConfigCard(miuix)`（默认 `DefaultBackendConfigCard(rows)`）；删 `id`/`customConfig`；`Backends.isSelectable=spec.selectable`；`register` fail-closed；builtins release 不含 debug probe。
- 迁移：43499Glued→ProfileCard、CVE43284→原卡、Lua→CustomLuaConfig、43284Glued/原版43499/probe→默认卡；`BackendId.kt` 删除（2 处 id override 移除）；`BackendPage` 双端单路径 `ConfigCard`；`BackendSessionStore` legacy CVE 前缀抽纯函数并写回 canonical key。
- 门禁：全量 JVM **434/0/0/0**；debug/androidTest/release 构建 PASS；`git diff --check`；`BackendId`/`customConfig` 在 app/src 为 0。
- 设备（A301SO）：debug 菜单 6 项（43499-Glued/43284Glued/2 个禁用原版/自定义/调试探针），逐项卡渲染正确、禁用项不可选、probe 默认卡可见；**release 菜单 5 项无调试探针**；`DebugProbeHostSmokeTest` OK(2)、`FlowHostProcessSmokeTest` OK(4) 回归 PASS。
- 备份：`/tmp/ghostlock-backend-registry-backup-20261102T224155Z`（MANIFEST `73905d50…`）。
- UNVERIFIED：release 下运行 probe（不可选已由菜单验证）；SessionStore 旧 prefs 真机迁移；动态 Lua spec 新卡设备走查。
