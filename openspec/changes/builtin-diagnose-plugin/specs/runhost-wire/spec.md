## ADDED Requirements

### Requirement: Builtin plugin archive handoff

The system SHALL extend the existing RunHost request only with the plugin archive PFD and semantic content root, preserving the existing business method set and profile shape. BXP3-B1's approved exact contract, scope and original device gates remain defined once in [design](../../design.md#record-gl-dek); this delta does not publish it as already implemented.

#### Scenario: Equivalent content is repacked

- **WHEN** the same plugin content is repacked with different ZIP entry order, timestamps or compression
- **THEN** the semantic content root remains identical, and whole-archive SHA-256 or ZIP byte count is not introduced as a wire identity field.

#### Scenario: Both execution domains must retain their original evidence gate

- **WHEN** B1 is assessed for its ARM64 device closure
- **THEN** App and Shizuku each require their original plugin/native/process-lifecycle evidence; absent Shizuku evidence does not close B1-V3, and package/build success is not process-death evidence.
