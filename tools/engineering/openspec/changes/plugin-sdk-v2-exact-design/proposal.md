## Why

P-BACKEND-X-002/决策04已经由用户于2026-11-22明确认可，并只开放 **SDK-v2 exact design gate**。当前 `plugin/api` 仍只有 Diagnose 专用 `DiagnosticBackend*`，当前 manifest/core/loader 仍是 format 1.0、单 BACKEND、Android ARM64/`diagnostic-v1` vertical；决策04中的 Java/KMP interface 和 manifest JSON 只是角色骨架，不足以让正式 backend/provider/Tool/Custom Executor 独立编译，也不足以让 Host 对失败、取消、close、trust、provider binding 和 capability 做无歧义处理。

真实消费者是：builtin Diagnose 单轨迁移、43499 backend 与其专属 physical provider、backend-neutral extractor/serializer/toolchain、43284 KO/toolchain、98260 profile，以及唯一 Custom Executor 的 Lua/JavaScript/WebUI/raw-memory bridge。本 change 沿用 P-BACKEND-X-002，不重开决策04的四角色、plugin-owned Compose/native、单 catalog 或 KMP 模块裁决。

## What Changes

- 完整冻结 format 2 manifest JSON Schema：package/variant/payload/files/resources/i18n/components、四种 component discriminated union、dependency/export/provider-slot、API/target/version/value domain 与拒绝语义。
- 完整冻结 `:plugin:api` 的 Java 17/JVM binary contracts，以及 `:plugin:ui-api` 的 KMP Compose contracts；列出全部 public type、method、nullability、exception/error、threading、blocking、ownership、cancel、close 与 terminal-state 语义。
- 完整冻结 Document/Artifact/PluginValue、ordinary dependency、Custom capability、raw-memory、HostOperations、preparation publish、provider binding、execution trust、i18n 和 WebUI typed bridge。
- 给出 UI session、preparation job、backend run、Custom execution、capability lease 和 revoke/cutoff 的状态机与逆序关闭规则；不存在面向调用者的异步 polling API。
- 定义 API major/minor compatibility、protected namespace、compileOnly/BOM、ABI dump、conformance fixture 和 architecture fitness functions。
- 给出下一 SDK source batch 的逐模块/逐文件范围与验证矩阵，但本 change 不写 production source。

## Capabilities

- `plugin-manifest-v2-exact`：可由 JSON Schema 和 core model 唯一表达/验证的 package、payload、component、binding、dependency、export 与 target 合同。
- `plugin-runtime-api-v2-exact`：Backend/Preparation/Custom、HostOperations、result/error/cancellation/close 和 ownership 的完整 Java ABI。
- `plugin-ui-api-v2-exact`：Backend/Provider/Tool/Custom Compose session、嵌套 handle、status/snapshot/state/i18n/preparation/WebUI 的完整 KMP API。
- `plugin-value-capability-v2-exact`：Document/Artifact/PluginValue、dependency/capability/raw-memory 与 typed bridge 的完整 value/handle 合同。
- `plugin-lifecycle-trust-v2-exact`：UI/preparation/runtime/Custom 的统一 execution-trust gate、并发状态机、cutoff、revoke、abort 和逆序终结语义。

## Impact

- 父架构 authority 保持为 `tools/engineering/openspec/changes/plugin-architecture/design.md#record-plugin-sdk-v2`；本 change 只把其明确预留的 exact-design gate 展开为可实施合同。
- 下一 source batch 将影响 `plugin/api`、新增 `plugin/ui-api`、升级 `plugin/core`、新增 `plugin/testkit` 与 plugin build logic；本批只定义这些变更，不实施、不迁移 Diagnose、不接真实攻击 backend。
- 当前 `diagnostic-v1`、Host `BackendPresentationRegistry`、manifest 1.0 和 loader 硬编码保持 current-tree 事实，直到后继 Host/Diagnose migration batch 单轨替换；不新增 v1/v2 compatibility adapter。
- Beads 只记录本 exact-design gate 的状态、审查、批准与实际证据；OpenSpec 保存唯一完整正文和验收定义。
