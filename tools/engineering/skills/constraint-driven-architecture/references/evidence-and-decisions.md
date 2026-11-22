# Evidence and decision model

Use these small structures when confidence affects an architectural choice.

## Evidence ledger

| ID | Claim or constraint | Class | Source | Architectural effect | Validation / owner |
|---|---|---|---|---|---|
| E1 | Example: writes must survive reconnect | USER-STATED | user brief | requires durable local state and reconciliation | test reconnect behavior / product owner |

Classes: `VERIFIED`, `USER-STATED`, `DERIVED`, `ASSUMED`, `UNKNOWN`, `REQUIRES-MEASUREMENT`.

For each `UNKNOWN`, mark one disposition: `architecture-changing`, `keep reversible`, `experiment`, or `safe to defer`. Never turn an assumption into a capacity fact.

## Constraint matrix

| Dimension | Current evidence or scenario range | Requirement / target | Sensitivity | Test or decision |
|---|---|---|---|---|
| Load and latency | initial / moderate / growth | target or unknown | low / medium / high | benchmark, keep simple, or redesign |
| Consistency and recovery | fact or assumption | invariant, RTO, RPO | low / medium / high | failure test or ADR |
| Security and compliance | data class and threats | control or unknown | low / medium / high | threat model / owner |
| Team, budget, operations | people and capacity | feasible operating model | low / medium / high | confirm ownership |

## Decision record

For each consequential decision capture:

1. **Decision:** what is selected.
2. **Context:** constraints and evidence that led to it.
3. **Alternatives:** serious candidates and why they lost.
4. **Assumptions:** what must remain true.
5. **Reversibility:** cheap, staged, or expensive to change.
6. **Invalidation:** observable signal that reopens the decision.
7. **Fitness function:** a runnable or reviewable control, if practical.

## Measurement rule

When reasoning cannot establish a claim, write the smallest useful experiment: workload, representative data, environment, metric, pass/fail threshold, owner, and date or trigger. Prefer a measurement plan to an invented percentile, capacity number, or availability promise.
