## ADDED Requirements

### Requirement: Runtime entry contracts are complete and blocking

The system SHALL expose Java 17 interfaces for Backend, Preparation, Custom execution and capability-provider entries. `execute`/`invoke` calls SHALL be blocking and terminal for that call; the public runtime API SHALL NOT require polling, futures, arbitrary Host DI access or implementation-class exchange.

#### Scenario: A backend run is created and executed

- **WHEN** the resolver has frozen an eligible exact closure and calls the backend entry
- **THEN** the plugin receives one immutable `BackendRunContext`, returns one valid terminal report or a checked contract failure, and is closed exactly once after execution returns.

### Requirement: Contexts expose only lifecycle-scoped named capabilities

The system SHALL give Backend, Preparation, Custom and capability-open lifecycles separate immutable contexts. They SHALL expose identity, exact handles, declared dependency/Host-operation views, cancellation and logging appropriate to that lifecycle, but SHALL NOT expose Host container, ClassLoader, Compose objects, JNA objects, native pointers or unrestricted reflection services.

#### Scenario: Ordinary backend asks for an undeclared dependency

- **WHEN** it requests a dependency id absent from its frozen manifest binding view
- **THEN** lookup fails with a closed error and does not enumerate or search the global catalog.

### Requirement: Domain reports and transport failures remain distinct

The system SHALL represent backend domain completion, external-effect state, ordered stage facts and published outputs independently from Host transport/process/workspace facts. Exceptions SHALL never be interpreted as domain success; process death or malformed result SHALL preserve `UNKNOWN`/`HELD` where cleanup or external effect cannot be proved.

#### Scenario: RunHost dies after a native write may have occurred

- **WHEN** no valid terminal report and cleanup proof are available
- **THEN** Host reports unknown completion/effect or held effect and does not claim rollback merely because the process exited.

### Requirement: Host operations are declared, typed and non-enumerable

The system SHALL expose Payload, Finish, process/artifact and future Host services through versioned declared Host-operation ids with typed `PluginValue` requests/results. A backend SHALL obtain only the operations frozen for its manifest and execution domain.

#### Scenario: A plugin invokes an undeclared or wrong-major Host operation

- **WHEN** the operation id was not bound or its contract major is incompatible
- **THEN** invocation fails before the Host side effect and is recorded as a contract/authorization failure.

### Requirement: Runtime ownership terminates in reverse dependency order

The system SHALL close backend/custom execution, capability leases, provider/tool services, materializations, classloaders and workspace in reverse acquisition/dependency order. Plugin Kotlin/Java SHALL remain the only owner of its DSO mapping and plugin-private native handles.

#### Scenario: Entry construction fails after opening two dependencies

- **WHEN** the second dependency or entry constructor fails
- **THEN** every acquired lease is closed once in reverse order, no backend execute call occurs, and Host does not invoke plugin-native cleanup on the plugin's behalf.
