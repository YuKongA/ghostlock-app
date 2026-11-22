# 活跃计划最终目标全局 UML（**目标时间视点权威图**）

<a id="global-active-plan-target-view"></a>

> **时间视点**：当前有效活跃计划按依赖顺序全部完成后的合并目标。当前工作树事实见[当前状态全局 UML](full-process-uml.md#global-current-view)。
>
> **状态边界（2026-11-22）**：本图投影 [P-BACKEND-X-002/决策04](../../tools/engineering/openspec/changes/plugin-architecture/design.md#record-plugin-sdk-v2) 的重新 CDA 设计，不声称 v2 SDK、plugin Compose UI、backend-bound provider、Custom Executor、KMP App 或真实攻击 backend 已实现/获批/通过门禁。当前 `diagnostic-v1`/Host Diagnose UI 仍只能由当前图表达。决策03与 gl-9lz 旧 exact design 只作沿革，不再作为本时间视点的目标 authority。

## 1. 最终目标组件与 owner

```mermaid
flowchart TB
  subgraph DIST[Distribution and catalog]
    SRC["BUILTIN_SEED / USER_IMPORT / future repository"]
    VERIFY["one verifier + immutable installer"]
    CATALOG["one catalog / registry authority<br/>identity / provenance / enabled / active variant"]
    TRUST["one ExecutionEligibilityResolver<br/>exact version + semantic content root"]
    SRC --> VERIFY --> CATALOG
    CATALOG --> TRUST
  end

  subgraph PKG[Atomic editable .glplugin.zip]
    BACKEND["BACKEND<br/>Compose UI + runtime orchestration"]
    PROVIDER["BACKEND_PROVIDER<br/>one parent backend + one slot<br/>Compose UI + data/jobs"]
    TOOL["TOOL<br/>backend-neutral finite jobs<br/>optional Compose UI"]
    CUSTOM["CUSTOM_EXECUTOR<br/>one active Lua / JavaScript / WebUI executor"]
  end

  subgraph UI[UI process / shared KMP Compose]
    SHELL[":app:shared<br/>navigation / page shell / generic Run action"]
    UILOADER["verified UI loader<br/>no native search path"]
    I18N["one KMP locale/message registry"]
    BUI["BackendUiSession.Content"]
    PUI["BackendProviderUiSession.Content"]
    TUI["optional ToolUiSession.Content"]
    CUI["Custom Executor UI + WebUI container"]
    ANDROID[":app:android<br/>GhostLockAndroidApplication"]
    DESKTOP[":app:desktop"]
    ANDROID --> SHELL
    DESKTOP --> SHELL
    SHELL --> UILOADER
    I18N --> BUI
    I18N --> PUI
    I18N --> TUI
    I18N --> CUI
    UILOADER --> BUI
    UILOADER --> CUI
    BUI -->|"selects and embeds"| PUI
    PUI -->|"may embed declared dependency"| TUI
  end

  subgraph PREP[Preparation and immutable inputs]
    REV["plugin UI-owned draft semantics<br/>Host-carried revision/status"]
    JOB["Provider / Tool finite job"]
    DOC["immutable Document"]
    ART["immutable Artifact / user KO / script bundle"]
    REV --> JOB
    JOB --> DOC
    JOB --> ART
  end

  subgraph RUN[Runtime]
    CUTOFF["Host cutoff<br/>backend execution config + exact provider bindings<br/>package closure"]
    RUNHOST["fresh App / Shizuku / Desktop RunHost"]
    DEPS["ordinary non-enumerable DependencyView"]
    BENTRY["generic BackendEntryPoint / BackendRun"]
    BEXEC["plugin-owned backend orchestration"]
    NATIVE["plugin Kotlin/Java-owned DSO load / call / close"]
    HOSTOP["HostOperation<br/>Payload / Finish / log / artifact"]
    CUTOFF --> RUNHOST --> DEPS --> BENTRY --> BEXEC --> NATIVE
    BEXEC --> HOSTOP
  end

  subgraph CRUN[Privileged Custom runtime]
    CSESSION["frozen Custom session package set"]
    CDIR["enumerable CUSTOM_CALLABLE directory"]
    SCRIPT["Lua / JavaScript input artifact"]
    RAW["raw-memory and semantic capability handles"]
    CSESSION --> CDIR
    SCRIPT --> CDIR --> RAW
  end

  CATALOG --> PKG
  TRUST --> UILOADER
  CATALOG --> I18N
  TRUST --> REV
  TRUST --> CUTOFF
  TRUST --> CSESSION
  BACKEND --> BUI
  BACKEND --> BENTRY
  PROVIDER --> PUI
  PROVIDER --> JOB
  TOOL --> JOB
  TOOL --> TUI
  CUSTOM --> CUI
  CUSTOM --> CSESSION
  DOC --> CUTOFF
  ART --> CUTOFF
```

目标不变量：

- Catalog 是 installed membership/exact object/provenance/active variant 唯一 authority；UI、preparation、ordinary dependency 和 Custom directory 只是不同权限投影。
- Imported executable 完整性通过后仍需独立 execution grant；UI/preparation/runtime/Custom 共用一个 eligibility resolver，新 version/content root 不继承旧 grant。
- Backend 自己绘制执行配置 UI，并选择/嵌入与自己 parent+slot+contract 双向匹配的 provider UI；Host 不维护 settings schema/DSL。
- Backend configuration Document 只保存 backend-owned execution config；provider identity、physical Document/Artifact 与 revision 只存在于 exact provider binding，不双写。
- Provider 只属于一个 parent backend；Tool 是 backend-neutral；`extractor -> backend-bound provider -> backend` 是领域调用方向。
- Backend runtime 自己编排攻击步骤；Host `Flow` 不是 plugin ABI。plugin Kotlin/Java 始终拥有 native load/call/close，Host 不持有 JNA mapping/pointer/symbol/native handle。
- Custom 是一个 active execution plugin；script/WebUI 是 input artifact，不是 component。Custom 可枚举本 session 已加载 package 的显式 `CUSTOM_CALLABLE` capability，包括指定地址 raw-memory 读写。
- builtin/imported 共用 verifier/installer/catalog/loaders；差异只是 provenance/trust/seed/update/uninstall policy。
- App 与 plugin UI 共用 KMP Compose 和同一 i18n registry；Android `R.string` 不是 plugin ABI。

## 2. 目标 identity/class 关系

```mermaid
classDiagram
  class PluginPackageManifestV2 {
    publisherId
    packageId
    version
    variant
    uiPayload
    runtimePayload
    filesSha256
  }
  class BackendComponent {
    backendKey
    uiEntry
    runtimeEntry
  }
  class BackendProviderComponent {
    parentBackendRef
    slotId
    contractVersion
    uiEntry
  }
  class ToolComponent {
    jobs
    uiEntry_optional
  }
  class CustomExecutorComponent {
    uiEntry
    runtimeEntry
    languages
  }
  class ProviderSlot {
    slotId
    contractId
    acceptedVersion
  }
  class BackendUiSnapshot {
    revision
    executionConfigDocument
    providerBindings
  }
  class ProviderUiSnapshot {
    providerIdentity
    slotContractIdentity
    revision
    physicalDocumentArtifacts
  }
  class DependencyView {
    nonEnumerable
  }
  class CapabilityDirectory {
    enumerableCustomCallable
  }
  class PluginDocumentHandle
  class PluginArtifactHandle
  class PluginCapabilityHandle
  class BackendRunReport {
    completion
    effectState
    stages
  }

  PluginPackageManifestV2 "1" o-- "0..*" BackendComponent
  PluginPackageManifestV2 "1" o-- "0..*" BackendProviderComponent
  PluginPackageManifestV2 "1" o-- "0..*" ToolComponent
  PluginPackageManifestV2 "1" o-- "0..*" CustomExecutorComponent
  BackendComponent "1" o-- "0..*" ProviderSlot
  ProviderSlot --> BackendProviderComponent : mutual match
  BackendComponent --> BackendUiSnapshot : freezes
  BackendUiSnapshot --> PluginDocumentHandle
  BackendUiSnapshot --> ProviderUiSnapshot : exact binding refs
  ProviderUiSnapshot --> PluginDocumentHandle
  ProviderUiSnapshot --> PluginArtifactHandle
  BackendComponent --> DependencyView : runtime receives
  CustomExecutorComponent --> CapabilityDirectory : session receives
  DependencyView --> PluginCapabilityHandle
  CapabilityDirectory --> PluginCapabilityHandle
  BackendComponent --> BackendRunReport : runtime returns
```

## 3. UI 准备、run 与收尾 sequence

```mermaid
sequenceDiagram
  actor User
  participant Shell as Host KMP shell
  participant Catalog as Catalog / registry
  participant BackendUI as Backend Compose UI
  participant ProviderUI as Bound Provider Compose UI
  participant Tool as Tool preparation job
  participant Cutoff as Host cutoff resolver
  participant RunHost as App/Shizuku RunHost
  participant Backend as Backend plugin runtime
  participant Native as Plugin-owned native
  participant HostOp as HostOperation

  User->>Shell: select backend
  Shell->>Catalog: resolve verified backend UI entry
  Shell->>BackendUI: create + Content()
  BackendUI->>Catalog: providers(parent, slot, contract)
  Catalog-->>BackendUI: mutually matched handles only
  BackendUI->>ProviderUI: select + embed Content()
  opt import / extract / build
    ProviderUI->>Tool: submit declared finite job
    Tool-->>ProviderUI: immutable Document/Artifact + revision
  end
  BackendUI-->>Shell: generic status + revision
  User->>Shell: Run
  Shell->>BackendUI: freeze()
  BackendUI->>ProviderUI: freeze selected provider
  ProviderUI-->>BackendUI: exact provider snapshot
  BackendUI-->>Cutoff: opaque backend config + exact provider refs
  Cutoff->>Catalog: resolve exact runtime closure
  Catalog-->>RunHost: verified read-only package/input handles
  RunHost->>Backend: createRun(context).execute()
  Backend->>Native: plugin-owned load / call / close
  Backend->>HostOp: Payload / Finish / published operation
  Backend-->>RunHost: domain completion/effect/stages
  RunHost->>RunHost: reverse-close leases, logs, workspace, process
  RunHost-->>Shell: combined terminal report
```

## 4. 活跃计划依赖顺序

```mermaid
flowchart LR
  D4["Decision 04 architecture<br/>CDA + Compose feasibility + critic + user approval"]
  EXACT["SDK-v2 exact design gate<br/>complete declarations/schema/state machines<br/>critic + user approval"]
  SDK["SDK-v2 source<br/>api / ui-api / core / testkit / build plugin"]
  HOST["Host-v2<br/>catalog/resolver/UI loader/RunHost<br/>app shared/android/desktop layers"]
  DIAG["Diagnose migration<br/>remove diagnostic-v1 + Host Diagnose UI"]
  B43499["43499 backend + bound provider<br/>CPU/attempts + physical config"]
  TOOLS["generic extractor/serializer/toolchain"]
  CUSTOM["single Custom Executor<br/>Lua/JS/WebUI/raw memory"]
  NEXT["43284 / 98260 / more ABIs / full Desktop"]
  DEVICE["backend-specific AVD/device gates"]

  D4 --> EXACT --> SDK --> HOST --> DIAG --> B43499 --> TOOLS --> CUSTOM --> NEXT --> DEVICE
```

这个顺序不将后继节点自动转为 production 批准。决策04获用户认可后只开放 SDK-v2 exact design gate；该 gate 再获认可后才开放 SDK source。每个后继批仍按它的真实 consumer、证据与门禁单线推进。
