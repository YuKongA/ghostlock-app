## ADDED Requirements

### Requirement: Custom is one execution plugin, not a family of script components

The system SHALL permit at most one active `CUSTOM_EXECUTOR` component in a catalog generation. It SHALL own Lua and JavaScript engines, editor/runtime Compose UI, script input handling, capability bridging, and optional WebUI hosting. User scripts and WebUI bundles SHALL be input Artifacts, not plugin packages or component kinds.

#### Scenario: The catalog has zero or multiple Custom Executors

- **WHEN** Custom selection is absent or ambiguous
- **THEN** the Custom entry is explicitly `NOT_READY` and the Host does not choose by package order.

#### Scenario: A script calls another backend plugin

- **WHEN** the session directory contains a compatible `CUSTOM_CALLABLE` capability exported by that plugin
- **THEN** Lua or JavaScript invokes it through the versioned descriptor/value/handle bridge without importing the provider's private classes or asking the Host to perform its native call.

### Requirement: Custom exposes explicit low-level capabilities

The system SHALL provide a standard raw-memory capability capable of bounded reads and writes at an explicitly selected address space, target, and absolute address, plus separately identified owned-region allocation/query/free. The contract SHALL expose width, alignment, endianness, partial transfer, and native-error facts and SHALL NOT imply rollback of arbitrary writes.

#### Scenario: A script writes a specified address

- **WHEN** the user supplies a target, absolute address, and bytes/scalar and the selected plugin export supports that address space
- **THEN** the call reaches the exporting plugin's Kotlin/native implementation through the raw-memory handle and returns exact transfer/error facts; the Host does not own or reinterpret the pointer.

#### Scenario: A session closes after an arbitrary write

- **WHEN** the Custom RunHost and capability handles are destroyed
- **THEN** owned allocations are closed according to their contract, but external/process/device/kernel memory changes are not claimed to have been reverted.

### Requirement: Custom WebUI and execution use a typed bridge

The system SHALL display a script bundle's declared WebUI under the Custom Executor's Compose UI in the UI process and SHALL execute its Lua/JavaScript in the Custom RunHost. The two sides SHALL exchange only typed message values and artifact references through the Custom Executor-owned bridge.

#### Scenario: WebUI sends an execution request

- **WHEN** WebUI JavaScript posts a valid typed message
- **THEN** the Custom Executor validates and translates it into its script/session model; neither the Web page nor the Host obtains arbitrary JVM reflection, JNA objects, or private plugin instances.
