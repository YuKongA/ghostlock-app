## ADDED Requirements

### Requirement: One execution-eligibility decision gates every code entry

The system SHALL evaluate enabled state, exact package version/content root trust, API/target compatibility and frozen closure through one resolver before creating UI, preparation, backend runtime, Custom execution or capability code. Imported executable packages SHALL start `NOT_GRANTED`; import SHALL NOT imply execution grant.

#### Scenario: An imported UI package passes hash verification but lacks a grant

- **WHEN** Host attempts to render its UI or execute its preparation/runtime entry
- **THEN** every entry is denied before class loading and data-only objects remain separately usable according to provenance policy.

### Requirement: Grant and revocation bind exact content

The system SHALL bind `USER_GRANTED` to publisher/package/version/variant/semantic-content-root identity. New version, variant or content root SHALL NOT inherit it. Revocation SHALL deny new entry creation and close non-cutoff UI/preparation sessions; an already-cutoff run SHALL retain frozen facts and follow its cancellation/cleanup semantics.

#### Scenario: Package bytes change under the same claimed publisher and version name

- **WHEN** semantic content root differs
- **THEN** the old execution grant is inapplicable and no executable entry loads until a new explicit grant.

#### Scenario: Trust is revoked during a cutoff Custom execution

- **WHEN** its WebUI channel is active
- **THEN** Host closes that UI channel and rejects new messages but does not cancel or kill the frozen runtime solely because of revoke; only a separate explicit abort follows the component abort policy.

### Requirement: Lifecycle states and legal calls are closed

The system SHALL define legal states/transitions for UI session, preparation job, backend run, Custom execution, capability lease and output writer. Calls outside the state table SHALL fail as contract violations; `close()` SHALL be idempotent, non-concurrent with Host-owned execute/invoke except where explicitly stated, and terminal.

#### Scenario: Host closes a run while execute is still returning normally

- **WHEN** execute has not reached a terminal return
- **THEN** Host requests cancellation according to abort policy and waits or terminates the process; it does not concurrently call plugin `close()` on the live run object.

### Requirement: Abort policy has exact Host behavior

The system SHALL map `BEFORE_EXECUTE_ONLY` and `COOPERATIVE` to explicit UI availability, cancellation-token, `requestStop`, deadline and process-termination behavior. Cooperative cancellation SHALL NOT imply rollback; emergency process death SHALL produce unknown effect unless proven otherwise.

#### Scenario: User cancels a cooperative run after effect may be applied

- **WHEN** plugin returns `CANCELLED` but cannot prove cleanup
- **THEN** completion may be cancelled while effect remains `HELD` or `UNKNOWN`; Host does not rewrite it to `CLEANED`.

### Requirement: Cutoff freezes an immutable exact closure

The system SHALL freeze backend execution Document, exact provider bindings, dependency/capability/Host-operation bindings, package versions/variants/content roots, execution domain and catalog generation before RunHost creation. Active runs SHALL neither poll catalog nor hot-swap packages, configuration or outputs.

#### Scenario: Provider or catalog updates after cutoff

- **WHEN** a newer revision/package becomes active
- **THEN** the current run continues with its frozen closure; a new run must re-freeze and re-confirm the new facts.

### Requirement: Contract corruption taints only the disposable owner process

The system SHALL treat entry type mismatch, protected-namespace definition, invalid returned value/report, illegal lifecycle call and uncaught plugin throwable as contract corruption. Runtime corruption SHALL terminate/quarantine the disposable RunHost; UI corruption SHALL invalidate that component generation without synthesizing fallback UI.

#### Scenario: Plugin returns a malformed terminal report

- **WHEN** required invariants or identities are invalid
- **THEN** Host records contract failure, preserves unknown effect as needed, closes what remains safely, and never admits the malformed report as success.
