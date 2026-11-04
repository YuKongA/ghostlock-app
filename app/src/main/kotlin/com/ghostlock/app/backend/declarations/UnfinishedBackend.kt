package com.ghostlock.app.backend.declarations

/**
 * 「未完工」backend 标记（2026-11-03 用户裁定：统一 `backend/backends/`，未完工用注解标记）。
 *
 * 语义：`backend/backends/` 不再按 active/unfinished 两级目录区分 backend；是否可运行由
 * [BackendSpec.selectable] 与 [BackendSpec.flowSymbol] 决定。本注解只标记“未完工、暂未接线”，
 * **不表示弃用**：实现、测试、注册项（`Backends.all`）与 presentation 配置卡
 * 都必须保留；被标记的 backend 在菜单中灰显/不可选，禁止以“清理/归档”为名删除。
 *
 * 不变量（由 `UnfinishedBackendAnnotationTest` 守卫）：
 * - builtin 中 `selectable=false` 的 spec 必须带本注解；
 * - 带本注解的 spec 必须 `selectable=false`；
 * - 注解 `RUNTIME` 可见且 [reason] 非空；
 * - debug probe 不带本注解（debug-only 可运行，不属于未完工）。
 *
 * 启用流程：去掉本注解 + 接线 [BackendSpec.flowSymbol] + `selectable=true`（同批过全部门禁）。
 *
 * @property reason 未完工原因/缺口；必须非空，供 UI/诊断与评审追溯。
 */
@Target(AnnotationTarget.CLASS)
@Retention(AnnotationRetention.RUNTIME)
@MustBeDocumented
annotation class UnfinishedBackend(val reason: String)
