# Architecture output menu

Choose only sections justified by system risk. A small solo project may need the compact form; a regulated or distributed system may need the fuller form.

## Compact architecture brief

1. **Mode and recommendation** - selected architecture and one-sentence rationale.
2. **Product and constraints** - actors, critical flows, invariants, non-goals, and the few constraints that drive shape.
3. **Evidence ledger** - facts, assumptions, unknowns, and measurements that matter.
4. **Boundaries** - modules, data ownership, trust zones, identities, authorization, and key connections.
5. **Alternatives** - baseline, serious candidates, rejected options, and trade-offs.
6. **Failure and operations** - timeouts, retries, idempotency, degraded mode, backup/recovery, observability, and owner.
7. **Delivery** - implementation order, tests, deployment, rollback, and the first validation actions.
8. **Risks and fitness functions** - invalidation conditions and checks that protect the boundaries.

## Fuller package (add only when justified)

Add a constraint matrix, critical workflow table, connectivity map, data lifecycle, threat/abuse model, capacity model, recovery matrix, migration stages, compatibility period, ADRs, risk register, and meaningful diagrams. Each artifact must change a decision, expose ownership, or define verification; delete decorative artifacts.

## Candidate comparison

| Candidate | Satisfies constraints | Complexity / operations | Ownership / data | Failure and consistency | Delivery / rollback | Reversibility |
|---|---|---|---|---|---|---|
| Simple baseline | yes / gap | low / medium / high | clear / unclear | explicit / gap | safe / risky | cheap / staged / expensive |

## Implementation sequence

Order work by risk reduction, not by diagram position:

1. invariants, module boundaries, and repository foundation;
2. identity, authorization, and data model;
3. critical contracts and workflows;
4. observability and failure behavior;
5. representative tests and benchmarks;
6. deployment, migration, rollout, and rollback;
7. later extraction or optimization only after its trigger is measured.
