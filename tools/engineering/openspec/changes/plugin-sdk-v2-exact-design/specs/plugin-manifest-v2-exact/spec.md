## ADDED Requirements

### Requirement: Format 2 has one canonical package identity and content root

The system SHALL validate a format-2 `.glplugin.zip` against one normative JSON Schema and SHALL derive semantic content root from canonical manifest bytes that include every non-manifest regular-file path and SHA-256. ZIP ordering, timestamps, compression and comments SHALL NOT affect semantic identity.

#### Scenario: A package is repacked without changing declared content

- **WHEN** only ZIP container representation changes and every manifest/file byte remains equivalent
- **THEN** the package version, component identities and semantic content root remain unchanged.

#### Scenario: A package contains an undeclared or duplicate entry

- **WHEN** an archive contains a regular file absent from `files[]`, a missing declared file, duplicate normalized path, traversal, symlink or special entry
- **THEN** admission fails before catalog publication and no component becomes executable.

### Requirement: Payload and target declarations are exact

The system SHALL distinguish UI and runtime payloads, ordered multidex/JAR code, runtime-only native libraries, stable package resource ids, i18n bundles and target variant facts. KO/profile/config/source/WebUI/passive resources SHALL be declared and component-owned without becoming another component kind. One thin archive SHALL target exactly one canonical platform/architecture/ABI variant, except a code-free data package MAY be universal.

#### Scenario: UI code attempts to obtain a native search path

- **WHEN** a package places a DSO in the UI payload or references a runtime native library from its UI payload
- **THEN** admission fails; only the runtime loader may expose that package's verified native directory.

#### Scenario: Target facts disagree with variant identity

- **WHEN** the canonical variant says Android ARMv7 but native ABI, ELF class/machine or platform requirements say another target
- **THEN** admission fails rather than selecting a best-effort interpretation.

### Requirement: Components are a closed discriminated union

The system SHALL admit only `BACKEND`, `BACKEND_PROVIDER`, `TOOL` and `CUSTOM_EXECUTOR`, with common identity/entry/dependency/export fields and kind-specific fields required and forbidden by schema. Entry classes SHALL be defined by the declared payload and SHALL implement every contract implied by that component's declarations.

#### Scenario: A component declares an impossible role mixture

- **WHEN** a `TOOL` declares a backend key/provider slot or a provider omits its exact parent/slot/contract binding
- **THEN** schema or semantic validation rejects the package before class loading.

### Requirement: Dependency and provider binding resolve by stable contract

The system SHALL resolve ordinary dependencies by manifest dependency id, stable component selector, capability-or-static mode and contract major/minor range. Provider visibility SHALL additionally require mutual parent component identity, slot id, contract id and compatible version. Runtime cutoff SHALL bind the exact package version, variant and semantic content root selected from that stable match.

#### Scenario: A 43499 provider targets another backend or stale slot contract

- **WHEN** parent identity, slot, contract id or compatible major/minor does not match the selected 43499 backend declaration
- **THEN** the provider is absent from that backend's directory and cannot enter its snapshot or runtime closure.

### Requirement: Static exports do not require executable plugin code

Provider and Tool components MAY expose verified Document/Artifact resources through stable static exports without UI or runtime entry code. Static resolution SHALL NOT construct a plugin classloader or capability session.

#### Scenario: Upstream publishes a new data-only device profile

- **WHEN** a mutually bound provider package contains only verified static profile resources and no UI/runtime payload
- **THEN** the backend may select and freeze that exact provider binding without loading plugin code or requiring a new APK/backend package.

### Requirement: API compatibility is monotonic within a major

The system SHALL record built-against and minimum-host minor versions separately for runtime API and UI API. Major mismatch SHALL reject; a host minor below the declared minimum SHALL reject; a newer host minor SHALL remain compatible only through new optional interfaces/features and SHALL NOT add abstract methods to an already released interface.

#### Scenario: A plugin needs an optional newer feature

- **WHEN** its manifest declares that feature and a minimum Host minor greater than the installed Host
- **THEN** the component is `NOT_READY` before entry construction, not loaded with a missing-method failure.
