## ADDED Requirements

### Requirement: Cross-component values are immutable and closed

The system SHALL exchange only immutable `PluginValue` null/bool/signed/unsigned/decimal/string/bytes/list/object values plus Document/Artifact handles for larger or streamed content. Object keys SHALL be unique and deterministically ordered; implementation objects SHALL NOT cross component classloaders.

#### Scenario: A capability returns a plugin-private object

- **WHEN** an implementation attempts to return an arbitrary JVM, JNA or native wrapper instead of an SDK value/handle
- **THEN** the boundary rejects it as contract violation and closes any partially created lease.

### Requirement: Document and Artifact handles carry exact identity and caller-owned reads

The system SHALL expose schema/kind, content SHA-256, provenance, target facts and immutable object id. Each open SHALL return a new caller-close stream/channel; materialized files SHALL be read-only, lifecycle-bound leases and SHALL NOT transfer ownership of Host storage.

#### Scenario: A handle content digest changes or its lease is closed

- **WHEN** reopen verification disagrees or a caller reads after close
- **THEN** access fails closed and the stale/corrupt object cannot enter preparation or runtime.

### Requirement: Preparation publication is atomic

The system SHALL create Document/Artifact outputs through a begin-write/publish/abort owner. Publish SHALL validate declared output id, schema/kind, target and content digest before returning an immutable handle; close without publish SHALL abort and remove only that owned transient.

#### Scenario: A job fails after writing half an output

- **WHEN** no successful publish occurs
- **THEN** no catalog-visible handle is created and reverse cleanup removes the partial owned transient.

### Requirement: Ordinary and Custom capability discovery remain distinct

The system SHALL make `DependencyView` non-enumerable by manifest dependency id and distinguish callable capability bindings from immutable static Document/Artifact bindings. Custom `CapabilityDirectory` SHALL enumerate only trusted, target-compatible `CUSTOM_CALLABLE` capability exports frozen for that session.

#### Scenario: Ordinary backend attempts global capability discovery

- **WHEN** it lacks a declared dependency id
- **THEN** no API reveals installed packages or Custom exports; only Custom Executor may enumerate its frozen directory.

### Requirement: Static dependency binding is code-free

Opening a capability SHALL return a lifecycle-bound handle with an exact descriptor and contract version. Opening a static dependency SHALL return only exact immutable Document/Artifact handles and SHALL NOT construct provider code.

#### Scenario: A provider consumes a static profile pack

- **WHEN** its declared dependency mode is `STATIC`
- **THEN** it receives the frozen exact resource handles and cannot invoke that slot as a capability.

### Requirement: Raw memory is an explicit typed standard capability

The system SHALL define a standard raw-memory contract that can enumerate address spaces, perform bounded absolute-address reads/writes, manage owned regions and return partial-transfer/native-error facts. A checked SDK adapter SHALL validate and take ownership of the generic exact export handle before exposing typed operations. Close SHALL release owned resources but SHALL NOT claim rollback of completed arbitrary writes.

#### Scenario: Custom writes a specified kernel address

- **WHEN** a trusted loaded export advertises writable kernel address space and the script requests an aligned bounded write
- **THEN** the export performs or rejects the exact write and returns transferred length/native status; Host does not perform the native write or retain its pointer.

### Requirement: Value resource limits are explicit and queryable

The system SHALL expose the current value depth/node/string/byte/encoded-size budget to callers, guarantee the SDK minimum budget, and use Document/Artifact handles above it. Resource exhaustion SHALL be a closed error without a partial capability or Host operation side effect.

#### Scenario: WebUI sends a value beyond the active budget

- **WHEN** depth or encoded size exceeds the session limits
- **THEN** decoding fails before handler invocation and the Custom session remains governed by its declared error policy.
