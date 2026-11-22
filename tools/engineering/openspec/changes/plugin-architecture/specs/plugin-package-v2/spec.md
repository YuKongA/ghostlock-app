## ADDED Requirements

### Requirement: A verified multi-component plugin package

The system SHALL distribute plugins as ordinary editable `.glplugin.zip` archives whose manifest lists the SHA-256 of every non-manifest regular file and may declare multiple `BACKEND`, `BACKEND_PROVIDER`, `TOOL`, and `CUSTOM_EXECUTOR` components. ZIP bytes, ZIP byte size, payload byte size, and compression layout SHALL NOT be package identity fields, and the verifier SHALL NOT impose the rejected fixed archive/entry/manifest/expanded-file limits.

#### Scenario: Equivalent content is repacked

- **WHEN** entry order, timestamps, compression method, or ZIP comments change while manifest-declared file bytes remain identical
- **THEN** component/file identity remains identical, ZIP CRC remains a container check only, and no whole-ZIP SHA-256 is required.

#### Scenario: One archive groups a backend and its provider

- **WHEN** one archive declares a backend and one mutually bound provider component
- **THEN** both enter the same atomic installed package object while remaining distinct components with distinct UI/runtime entries and lifecycle roles.

#### Scenario: A code-bearing logical release supports several targets

- **WHEN** the publisher produces Android ARM64, ARMv7, x86, x86_64, or desktop variants
- **THEN** each target is a separate thin archive for the same logical release and the installer selects only target-compatible variants; a data-only release may be universal.

#### Scenario: A package uses multidex

- **WHEN** its UI or runtime payload requires more than one DEX
- **THEN** the corresponding ordered payload code list admits all declared DEX files and the verifier rejects missing, extra, duplicate, or unhashed code rather than limiting the package to one DEX.

### Requirement: Builtin and imported packages share the runtime path

The system SHALL pass builtin seed and imported archives through the same verifier, immutable object store, catalog, component resolver, UI loader, and runtime loader. Their differences SHALL be limited to provenance, initial trust, seed reconciliation, update source, and disable/uninstall policy.

#### Scenario: The APK is installed for the first time

- **WHEN** the catalog has not reached READY
- **THEN** the builtin index is idempotently admitted through the ordinary installer before selection or execution is published, and builtin implementation/native classes are not merged into the Host runtime path.

#### Scenario: A user imports code

- **WHEN** an imported archive passes structural and integrity checks but no publisher-authentication scheme exists
- **THEN** it is recorded as `USER_IMPORT_UNVERIFIED`; file hashes establish exact integrity but do not claim publisher authenticity, and any executable payload remains `NOT_GRANTED` until a separate explicit high-risk execution grant.

#### Scenario: Imported executable trust changes

- **WHEN** a user revokes a grant, or the package version or semantic content root changes
- **THEN** the single execution-eligibility resolver rejects new UI, preparation, runtime, and Custom entry creation; a changed package does not inherit the old grant, while an already-cutoff run follows its frozen cancellation and cleanup semantics.
