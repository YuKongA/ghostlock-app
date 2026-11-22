## Why

当前已实现的 BXP3-B1 只是一个为 Diagnose 收窄的 vertical：`manifest 1.0` 只允许一个 `BACKEND`，public ABI 是 `DiagnosticBackend*`，Android loader 硬编码 `diagnostic-v1`/Diagnose namespace/arm64，UI 则依靠 Host 内置 `BackendPresentationRegistry`。它不能承载已确认的 43499 物理配置与执行配置、backend 专属 provider UI、通用 extractor/toolchain、一个 Lua/JavaScript Custom Executor、WebUI、多 ABI 和 Android/Desktop 共享 Compose UI。

用户已纠正“从 Diagnose 的局部 consumer 反推公开接口”的顺序：先完成能被正式插件依赖的长期 SDK，再迁移 Diagnose/43499/43284/98260；不保留无外部 consumer 的临时 `diagnostic-v1` 兼容层。

## What Changes

- 以 P-BACKEND-X-002/决策04 整体取代决策03的 Host 声明式 UI、普通 `PROVIDER` 与 `CUSTOM` component 模型；历史批准不自动授权新设计实现。
- 定义 v2 插件系统的完整 owner、角色、调用方向与生命周期架构，并固定 SDK exact design 必须覆盖的最小方法面；下一批先完成可编译 contract/schema/state machine 的 exact design gate，获批后才进入 SDK source。真实 backend 不允许再依赖临时 public API。
- component 只保留四个真实调用角色：`BACKEND`、严格绑定一个父 backend 的 `BACKEND_PROVIDER`、backend-neutral `TOOL`、全局一个活动的 `CUSTOM_EXECUTOR`。用户 Lua/JavaScript/WebUI 是 Custom Executor 的输入，不是 component 或 package。
- plugin 自己编写 Compose UI；backend UI 选择、装载并嵌入专属 provider UI。Host 不建 UI schema/DSL，不解释 CPU、尝试次数、profile 或 KO 字段。
- Host 维护一个 verified catalog/registry authority，但只负责准入、完整性、兼容、依赖解析、classloader、状态搬运、run cutoff 与 lifecycle；领域组合由 backend/provider/Custom Executor 拥有。
- backend runtime 自己实现编排并调用 plugin-owned native；Host `Flow` 仅是迁移期内部 adapter，不进入 public SDK。
- builtin 与 imported 仍走同一 installer/catalog/loader；差异只是 provenance/trust/seed/update/uninstall policy。

## Capabilities

- `plugin-package-v2`：多 component 可编辑 `.glplugin.zip`、thin target variant、UI/runtime payload、文件 SHA-256 列表与组件依赖。
- `plugin-compose-ui-v2`：Backend/Provider/Tool/Custom Executor 的 Compose UI lifecycle、同一 i18n registry、opaque snapshot 与 backend-owned 嵌套。
- `plugin-runtime-v2`：Backend/Provider job/Tool job/Custom Executor runtime、普通 dependency view、Custom capability directory、结果/取消/清理语义。
- `backend-provider-binding-v2`：parent backend identity + slot + contract version 双向匹配，阻止 43499 provider 串到其它 backend。

## Impact

- `plugin/api`、`plugin/core`、`plugin/packages/diagnose`、App catalog/loader/UI 的当前 v1 结构将在获批后由 v2 单轨替换，不并行保留 `diagnostic-v1`。
- `gl-9lz` 的“配置独立更新无需重装 APK”产品目标保留，但其 Host 编排 data-only provider/UI-free exact design 被本决策取代，须按 backend-bound provider 重新细化后才能实施。
- `gl-tz3`、`gl-r8h`、`gl-m0f`、`gl-2u2`、`gl-2ot`、`gl-75t` 保留为真实消费者/平台迁移任务，但它们依赖先完成并验证 v2 SDK。
- 本变更是 L 级设计：本轮交付 OpenSpec/spec/目标 UML/任务状态，并在用户认可前完成 Android/Desktop 独立 Compose plugin 的最小跨 classloader feasibility proof；不改 production source，不把 proof 或设计 ready 当作 SDK 实施批准。
