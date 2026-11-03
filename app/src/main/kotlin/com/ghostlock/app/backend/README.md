# backend 目录政策（backends/ + @UnfinishedBackend）

> 权威计划：`docs/plan/backend-directory-cleanup.md` §11（2026-11-03 用户裁定：统一 `backends/`，未完工用注解标记）。
> 本文件只描述目录/包与保留政策；运行行为契约见框架实现与 `docs/development/full-process-uml.md` §11.9。

## 1 布局

```
app/src/main/kotlin/com/ghostlock/app/backend/
  declarations/   框架契约：BackendSpec/BackendRow/BackendSelection、FlowEntryRegistryAssembly、@UnfinishedBackend
  registry/       唯一注册表 Backends
  host/           FlowHostService/FlowHostProtocol/FlowEntryParams/HostProcessExec/HostRunPaths
  launcher/       App/Shizuku 进程启动器
  diagnostics/    诊断 fixture（BuildConfig.DEBUG 门控）
  backends/       所有 backend 平级放置（package com.ghostlock.app.backend.backends.<name>）
    cve43499glued/  package ...backends.cve43499glued（可运行）
    cve43284glued/  package ...backends.cve43284glued（可运行）
    debugprobe/     package ...backends.debugprobe（仅 debug 注册/可选）
    cve43499/       package ...backends.cve43499（含 victim/flow/generator/... 子包；@UnfinishedBackend）
    cve43284/       package ...backends.cve43284（含 flow；@UnfinishedBackend）
    lua/            package ...backends.lua（@UnfinishedBackend）
```

测试镜像与生产同构：`app/src/test/kotlin/.../backend/backends/...`、
`app/src/androidTest/kotlin/.../backend/backends/...`。

## 2 目录不区分 active/unfinished

- 2026-11-03 用户裁定：「没必要区分active和unfinished，统一放在backends，未完成的用注解标记」。
- 目录只表达“backend 实现位置”；**是否可运行**由 `BackendSpec.selectable` + `BackendSpec.flowSymbol` 决定：
  - 可运行：`selectable=true` 且 `flowSymbol` 已接线；
  - 未完工：`selectable=false`（菜单灰显/不可选），实现与配置卡保留，用 `@UnfinishedBackend` 标记。
- `debugprobe` 是 debug-only 可运行 backend（`selectable=BuildConfig.DEBUG`，release 不注册），**不是**未完工，不标注。

## 3 未完工 backend（@UnfinishedBackend，不是弃用）

- 语义：未完工、暂未接线；**不表示弃用**。被标记 backend 的实现、测试、`Backends.all` 注册项、
  配置卡（`BackendSpec.ConfigCard`）全部保留，菜单中灰显/不可选；`readiness()/statusTextRes()`
  表达未就绪原因。
- **禁止**：以「清理/弃用/归档」为名删除、裁剪、注释掉或移出 `backends/` 的任何被标记实现/测试；
  禁止把未完工 backend 改成不可达（注册与配置卡必须仍可解析到实现）。
- 不变量由 `UnfinishedBackendAnnotationTest` 守卫：
  - builtin 中 `selectable=false` ⇒ 必须带注解；
  - 带注解 ⇒ 必须 `selectable=false`；
  - 注解 `RUNTIME` 可见、`reason` 非空，且只标记 CVE-2026-43499 / CVE-2026-43284 / CUSTOM；
  - debug probe 不带注解。
- 当前未完工清单：`cve43499/BackendCve202643499`、`cve43284/BackendCve202643284`、`lua/BackendCustom`。
- 启用流程：去掉 `@UnfinishedBackend` → 接线 `flowSymbol` → `selectable=true`（同批过全部门禁并更新文档）。
- 唯一例外：用户明确裁定弃用并给出删除授权时另立批次执行；同批更新计划、UML、备份与门禁并保留回滚记录。

## 4 包名规则

- 所有 Kotlin 文件 `package` 与目录一致（门禁：package/dir mismatch = 0）。
- `backend/backends/<name>[/<子包>]` ⇒ `com.ghostlock.app.backend.backends.<name>[.<子包>]`。
- `declarations/` 只放框架契约（BackendSpec/BackendRow/BackendSelection、FlowEntryRegistryAssembly、
  `@UnfinishedBackend`）；具体 backend 声明不得再放回 `declarations/`。
- 迁移/改名时同步更新 main、test、androidTest 的 package/import/FQN（含 `FlowSymbol` 字符串与测试断言），
  并保持行为不变。

## 5 变更流程（摘要）

1. 先读在办计划，判定无冲突/重复/被推翻；
2. 备份改动/搬迁文件（可回滚），再移动+改包；
3. 门禁：mismatch=0、旧前缀=0、`compileDebugKotlin`、全量 `testDebugUnitTest`、
   `assembleDebug`/`assembleDebugAndroidTest`/`assembleRelease`、`git diff --check`；
4. 设备回归（debug 菜单/卡片、release 菜单、probe/B2/Shizuku/fixture）由 Lead 执行并归档；
5. 不 commit：提交由 Lead 在门禁全绿且无 writer 时统一执行。
