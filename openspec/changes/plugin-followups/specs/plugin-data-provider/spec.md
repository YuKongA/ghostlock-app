## ADDED Requirements

### Requirement: Independently updatable data-only profile provider

The system SHALL package kernel profiles as one independently versioned, data-only `.glplugin.zip` whose source HOCON is converted at build time into typed immutable Documents. The package SHALL enter the same verifier, immutable object store, catalog and resolver as builtin backend packages, while containing no DEX, native library or runtime provider entry point.

#### Scenario: Upstream adds a device profile without changing attack code

- **WHEN** a higher-version profile-provider archive containing a new exact kernel-release Document is explicitly imported
- **THEN** the user does not reinstall the APK, the current process keeps its already published READY generation, and the new provider becomes active only after a real App-process death and restart—not an Activity recreation—and after the next catalog generation is fully reverified.

#### Scenario: A user-imported package fails admission

- **WHEN** the selected archive has the wrong package identity, contains code/native payload, has an invalid Document/export/hash, or violates version activation
- **THEN** no catalog record changes, staging is removed, the current READY view remains usable, and the failure is reported without claiming publisher authentication; an immutable object already installed before a catalog-commit failure may remain unreferenced until the next safe bootstrap garbage collection.

#### Scenario: The process dies while creating an owned transient

- **WHEN** the App process dies after a provider archive/scratch path is created but before import cleanup, or after a Document source inode is created but before its pathname is unlinked
- **THEN** the next one-shot bootstrap, before reading the catalog or publishing a backend view, validates and removes only the exact catalog-staging or Document-prefix namespace owned by that writer—including a direct regular scratch child with the legacy `glplugin-verify-<non-empty-token>.entry` shape produced by the previous `File.createTempFile` writer—preserves catalog references and referenced immutable objects, then continues normal snapshot verification and object garbage collection; an unknown shape, symlink, special file, delete failure or directory-fsync failure leaves the catalog `FAILED` rather than broadening cleanup or publishing partial membership.

#### Scenario: The user restores the bundled provider

- **WHEN** the committed profile-provider record has `USER_IMPORT_UNVERIFIED` provenance and the user explicitly requests reset, whether that record is active in this process or only pending for the next start
- **THEN** the next-start catalog drops exactly that record after every remaining object verifies, the current one-shot READY or FAILED state does not change, and the next bootstrap restores the APK seed before safely collecting unreferenced objects.

#### Scenario: A committed imported provider object is corrupt on restart

- **WHEN** bootstrap fails because the exact fixed-identity profile-provider record has `USER_IMPORT_UNVERIFIED` provenance but its referenced object is missing or corrupt
- **THEN** the catalog stays `FAILED`, runs and new imports remain closed, and the reachable Path shell may offer only a narrow explicit reset that strict-decodes catalog metadata, removes exactly that record, verifies every remaining object, and atomically commits a next-start catalog without garbage collection or silent fallback.

#### Scenario: Catalog failure is not the recoverable imported-provider case

- **WHEN** catalog metadata cannot be strictly decoded, the removable imported profile record is absent or ambiguous, or any remaining object fails verification
- **THEN** the narrow reset performs no catalog write and does not become a general catalog repair interface.

#### Scenario: No profile matches the current kernel release

- **WHEN** the active provider exports no Document whose schema is `com.ghostlock.kernel-profile` and whose document id exactly equals `uname -r`
- **THEN** the optional Diagnose slot is absent, Diagnose remains runnable, the UI reports no exact match, and the system does not synthesize a profile or claim device support.

### Requirement: Typed kernel-profile Document

The system SHALL represent each resolved profile as a strict Document envelope with schema identity, version, document id and a JSON value body. Its package file SHA-256 SHALL be the exact run content identity, while Host code SHALL validate only envelope/release facts and SHALL NOT duplicate backend-specific offset, route or credential semantics.

#### Scenario: HOCON includes are converted by the package producer

- **WHEN** the profile package is built
- **THEN** every index entry is resolved within the provider source root, recursive includes and substitutions are applied, the profile overrides its explicitly mapped route preset and shared execution preset, signed 64-bit integers and nulls are preserved, unknown fields remain present, every source file is classified exactly once (85 files at the migration baseline), and no Typesafe Config runtime dependency enters the App, RunHost, plugin API/core or produced archive.

#### Scenario: A Document is bound to a backend requirement

- **WHEN** exactly one compatible Document matches a declared slot in the same READY catalog generation
- **THEN** the run cutoff freezes its exact provider object, component, path, schema, document id and file digest; later catalog writes cannot replace that binding.

#### Scenario: Multiple Documents ambiguously match one slot

- **WHEN** more than one enabled export matches the same schema and exact document id
- **THEN** catalog publication fails closed rather than selecting by package order, version heuristic or fallback.
