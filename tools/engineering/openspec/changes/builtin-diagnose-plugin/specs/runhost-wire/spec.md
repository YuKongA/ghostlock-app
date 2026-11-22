## ADDED Requirements

### Requirement: Builtin plugin archive handoff

The system SHALL extend the existing RunHost request only with the plugin archive PFD and semantic content root, preserving the existing business method set and profile shape. BXP3-B1's approved exact contract, scope and original device gates remain defined once in [design](../../design.md#record-gl-dek); this delta does not publish it as already implemented.

#### Scenario: Equivalent content is repacked

- **WHEN** the same plugin content is repacked with different ZIP entry order, timestamps or compression
- **THEN** the semantic content root remains identical, and whole-archive SHA-256 or ZIP byte count is not introduced as a wire identity field.

#### Scenario: Archive descriptor ownership crosses the RunHost boundary

- **WHEN** the catalog object master PFD is duplicated into a wire request, dispatched, decoded and copied into RunHost loader staging
- **THEN** the freezer retains the master; `toWireRequest` makes exactly one outbound duplicate owned by the request/dispatch lease; the endpoint closes Binder-decoded descriptors in `finally` after making independent RunHost-owned duplicates; partial duplication closes acquired ordinary artifacts and then the archive in reverse acquisition order; the owned holder transfers artifacts to staging and the archive to the loader exactly once, closes only its untransferred remainder, and each consumer closes its transferred descriptors after copy on both success and failure.

#### Scenario: A retry changes plugin semantic content

- **WHEN** the same `runId` and generation are retried with a different semantic content root or backend key
- **THEN** request matching returns `RUN_MISMATCH`; the request fingerprint includes the semantic content root but excludes file-descriptor numbers, ZIP byte count, whole-archive SHA-256 and Parcel bytes.

#### Scenario: Both execution domains must retain their original evidence gate

- **WHEN** B1 is assessed for its ARM64 device closure
- **THEN** App and Shizuku each require their original plugin/native/process-lifecycle evidence; absent Shizuku evidence does not close B1-V3, and package/build success is not process-death evidence.
