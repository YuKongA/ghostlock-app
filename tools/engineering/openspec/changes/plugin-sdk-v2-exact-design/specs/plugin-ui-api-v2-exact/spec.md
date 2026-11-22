## ADDED Requirements

### Requirement: Each UI role has a complete KMP Compose session contract

The system SHALL expose versioned KMP interfaces for Backend, Backend Provider, Tool and Custom Executor UI entries. Creation, `Content`, status publication, freeze and close SHALL have explicit UI-thread and ownership rules; Host SHALL render only its shell and SHALL NOT synthesize plugin field schemas.

#### Scenario: 43499 renders physical and execution configuration

- **WHEN** its backend UI opens a mutually bound provider lease
- **THEN** the backend renders CPU/attempt/retry controls, embeds provider `Content` at its chosen location, and freezes only its execution Document plus the provider binding reference.

### Requirement: UI readiness and freeze use one monotonic revision

The system SHALL require a session to publish monotonic status revisions. `freeze()` SHALL succeed only for the latest `READY` revision and SHALL return that exact revision; concurrent option/preparation/catalog changes SHALL make the attempted freeze stale rather than silently substituting data.

#### Scenario: Preparation completes after the user changes provider

- **WHEN** an older provider/job revision publishes after a newer selection
- **THEN** the stale output cannot make the backend ready or enter the new snapshot.

### Requirement: Backend owns provider selection and lease placement

The system SHALL give only Backend UI a provider directory scoped to its mutually matched slots. Opening a provider SHALL return a lifecycle-bound UI lease whose `Content`, status and freeze are controlled by the Backend; Host SHALL cache/validate and force-close leases but SHALL NOT select or route provider UI.

#### Scenario: Backend changes selected provider

- **WHEN** it atomically selects another compatible candidate through its scoped directory
- **THEN** the old snapshot/revision becomes invalid, the old Host-owned lease closes after its composition disposes, the new provider starts independently, and Host never treats both as selected bindings.

### Requirement: Preparation calls are synchronous at the SDK boundary

The system SHALL expose finite blocking preparation execution and explicit cancellation, not polling/future APIs. Plugin UI MAY use its own coroutine/thread to call it and publish `BUSY`/`READY`; the Host-provided controller SHALL reject blocking execution from the composition/UI thread.

#### Scenario: Extractor produces a Document

- **WHEN** provider UI invokes a declared extractor Tool from a worker context
- **THEN** the call returns only after atomic output publication or terminal failure, and provider UI may freeze the returned exact handle without an intermediate conf/json file.

### Requirement: Host and all plugin UI use one namespaced message engine

The system SHALL resolve Host and plugin message references through one locale/fallback registry. Message namespace SHALL be bound to verified package/component identity; Android resources and already-localized strings SHALL NOT cross the UI API.

#### Scenario: Locale changes while a plugin page is open

- **WHEN** Host changes the active locale
- **THEN** Host, backend, provider and Tool Compose content re-resolve their own message keys from the same locale decision without APK repackaging.

### Requirement: Custom WebUI is local and typed

The system SHALL expose a Custom-only WebUI lease for verified local assets with network/file navigation denied and a bounded `PluginValue` message bridge. Host SHALL bind that UI bridge only to the exact active Custom runtime execution from the same frozen component/session; Host SHALL transport typed messages without interpreting or executing them. The bridge SHALL NOT expose a Host object, arbitrary JavaScript interface, reflection or native object.

#### Scenario: WebUI requests a raw-memory operation

- **WHEN** local WebUI sends a typed script request
- **THEN** Custom Executor validates it, calls the frozen capability directory, and sends a typed result/error; WebUI cannot bypass the executor to call Host or JNA directly.

#### Scenario: No matching Custom runtime is active

- **WHEN** WebUI sends a request before binding, after runtime termination, or to a stale session
- **THEN** the scoped gateway returns `NOT_READY` or `CANCELLED` and never rebinds the request to another execution.
