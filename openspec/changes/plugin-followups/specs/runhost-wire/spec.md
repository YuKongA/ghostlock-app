## ADDED Requirements

### Requirement: Manifest-bound Document inputs replace the transitional global profile

The system SHALL replace the transitional required `profileArtifactId`/`ProfileSnapshot` run shape with zero or more typed Document bindings declared by the exact backend manifest. This delta supersedes BXP3-B1's temporary instruction to preserve the profile shape, while preserving the existing plugin archive PFD, Payload/Finish, terminal, process-lifecycle and descriptor ownership contracts.

#### Scenario: Diagnose runs without a matching profile

- **WHEN** its optional `kernel-profile` requirement has no exact match
- **THEN** the frozen request contains zero Document bindings and no fake profile artifact, and both App and Shizuku RunHost accept the structurally valid diagnostic request.

#### Scenario: Diagnose receives a matching profile

- **WHEN** the catalog projection freezes one matching Document
- **THEN** the request carries one binding from slot/schema/document identity to one read-only `DOCUMENT` artifact PFD, the RunHost loader checks it against the verified manifest before class lookup, and the plugin can only look it up by the declared slot.

#### Scenario: A Document binding is malformed or undeclared

- **WHEN** a binding has a duplicate slot, references a non-Document artifact, disagrees with its digest/schema, is not declared by the backend, or omits a required slot
- **THEN** preparation fails before plugin execution and all acquired descriptors/workspace resources follow the existing close and cleanup rules.

#### Scenario: The plugin reads the frozen Document

- **WHEN** `DiagnosticRunContext.findDocument("kernel-profile")` returns an input
- **THEN** each `openStream()` returns a new caller-closed stream over the exact staged bytes, the plugin verifies the declared SHA-256, and any open/read/digest failure cannot produce diagnostic PASS.

#### Scenario: A retry changes a Document binding

- **WHEN** the same run identity is retried with a different slot, schema, document id, provider-selected bytes or digest
- **THEN** the normalized request fingerprint differs and existing run-mismatch semantics apply; file-descriptor numbers and Parcel bytes remain outside the fingerprint.
