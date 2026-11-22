## ADDED Requirements

### Requirement: Backend plugins own backend-specific orchestration and native code

The system SHALL invoke every backend through the generic v2 backend entry/run/context/report contract. Backend Kotlin/Java SHALL own the order of backend-specific operations and SHALL load, call, and close its package's native libraries. Host `Flow` types, JNA mappings, pointers, symbols, native handles, and backend-private implementation classes SHALL NOT cross the public SDK.

#### Scenario: Diagnose migrates to v2

- **WHEN** the v2 Host and SDK replace the transitional vertical
- **THEN** Diagnose implements the generic backend contract and the `DiagnosticBackend*`/`diagnostic-v1` API and loader branches are removed in the same single-track migration rather than wrapped for compatibility.

#### Scenario: A backend calls its native primitive

- **WHEN** runtime orchestration reaches a plugin-owned native step
- **THEN** plugin Kotlin/Java performs load/call/close using its verified runtime native directory, while the Host owns only the RunHost/process/artifact boundary.

#### Scenario: RunHost dies after a possible effect

- **WHEN** process death or malformed results prevent proof of cleanup
- **THEN** the final report preserves `UNKNOWN` completion and `HELD` or `UNKNOWN` effect as applicable; process destruction is not reported as rollback of external or kernel state.

### Requirement: Registry projections match caller authority

The system SHALL derive ordinary dependencies and Custom capabilities from one verified catalog/registry authority. Ordinary backend and preparation contexts SHALL receive only non-enumerable manifest-bound dependency slots, while a Custom session SHALL receive an enumerable read-only directory of the exact loaded exports marked `CUSTOM_CALLABLE`.

#### Scenario: An ordinary backend requests a dependency

- **WHEN** its manifest binds one dependency slot
- **THEN** it can open only that exact capability handle and cannot enumerate unrelated installed or loaded components.

#### Scenario: A Custom session starts

- **WHEN** the Host resolves enabled, trusted, target-compatible package closure for the active Custom Executor
- **THEN** the session freezes exact package/export identities, exposes their `CUSTOM_CALLABLE` descriptors, and does not hot-add upgrades or newly installed packages to the active directory.

#### Scenario: A capability closes

- **WHEN** a run/session terminates normally or during partial construction failure
- **THEN** leases and plugin resources close once in reverse dependency/acquisition order, while external/kernel side effects remain represented by the report rather than being treated as owned allocations.

### Requirement: Preparation outputs are immutable and revision-bound

The system SHALL run provider/tool preparation as finite jobs with explicit input handles and parameters, atomic Document/Artifact publication, cancellation, and revision identity. Backend runs SHALL consume only exact outputs frozen by the backend UI snapshot.

#### Scenario: An extractor directly creates a Document

- **WHEN** a generic extractor processes a selected source
- **THEN** it may publish the canonical Document directly without an intermediate conf/json file, and a serializer Tool may later convert that Document into an external configuration file.

#### Scenario: An older job completes late

- **WHEN** its input revision is no longer current
- **THEN** the result is not admitted into the current provider/backend snapshot and cannot silently replace the output accepted by the user.
