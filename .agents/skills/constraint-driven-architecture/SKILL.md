---
name: constraint-driven-architecture
description: Design, evolve, or explicitly review software architecture from product capabilities, invariants, and verified constraints. Use primarily for greenfield systems, major new capabilities, architecture decisions, or repository-level architecture reviews; do not use for ordinary small changes, isolated bug fixes or refactors, generic programming questions, trivial library choices, or work where architecture analysis adds unnecessary overhead.
---

# Constraint-Driven Architecture

## GhostLock integration

Upstream: Liparakis/ConstraintDrivenArchitecture at `04d2e998f19b08be6ba7f697315f3a16ecdf2708`; runtime-only installation, with this local routing addition.

Use CDA for material architecture, not ordinary S/M fixes or implementation of an already approved design. [Change level](../../../docs/development/engineering-standards.md#engineering-change-levels) and architectural relevance are different: a non-architecture L change still needs its original design/review/approval gates, but not a full CDA analysis.

For a new design enter through [propose](../openspec-propose/SKILL.md); for a substantive revision use [update](../openspec-update-change/SKILL.md). Apply the relevant lenses directly to that same OpenSpec body, located by the named change or existing Beads spec_id; do not restart analysis already completed there. Exploration without a required plan may stay in the requested answer. Reference templates are a menu, not extra artifacts or gates. Do not create rollback artifacts, compatibility layers for unused designs, or a second evidence ledger; [project authority and tool ownership](../../../docs/development/documentation-standards.md#planning-tool-authority) remain the single source.

For scoped exploration, answer the named architectural unknown rather than expanding it into a full design delivery. The upstream Mandatory gates are applicable reasoning checks, not additional approval or product gates; retain checks that affect the current decision. Apply the output menu to the requested scope and reuse settled sections instead of producing a new complete report on every invocation.

For formal design handoff use [author self-check](../ghostlock-author-self-check/SKILL.md). CDA REVIEW and its internal review lenses are analysis modes, not independent reviewer identities: they never replace the non-author [design-critic](../ghostlock-design-critic/SKILL.md) required by [AGENTS §7](../../../AGENTS.md#agents-review). Optional internal delegation does not make that triggered review optional. Architectural reasoning does not prove device behavior; retain original validation and device gates.


Turn a product idea or material system change into the simplest defensible architecture that satisfies its security,
performance, reliability, compliance, organizational, delivery, and cost constraints.

## Operating law

Start with capabilities, invariants, failure consequences, and constraints. Increase distribution, infrastructure,
abstraction, or technology diversity only when evidence justifies the added coordination and operational cost. Treat a
modular monolith or similarly small deployable as the default baseline. Do not equate maturity with microservices,
Kubernetes, event streaming, multiple databases, service meshes, or a large diagram.

Keep the answer proportional to risk. Do not produce every artifact in the reference template for a small project. Do
not invent numbers, requirements, ownership, or technology capabilities. Do not silently assume web SaaS, cloud hosting,
a browser UI, a relational database, or managed services; label any illustrative choice as an assumption. Challenge a
requested architecture style when the stated facts do not support it.

## Select a mode

Choose one mode before analysis and state it briefly:

- **GREENFIELD** (default): design a new system from a product concept or requirements.
- **EVOLUTION**: change an existing system for a major capability or materially changed constraint.
- **REVIEW**: recover and evaluate the real architecture of an existing repository.

Use EVOLUTION when the existing system remains in service and the task is to add or migrate. Use REVIEW when analysis is
requested without an implementation change. If mode is ambiguous, choose the best-supported mode, state the assumption,
and proceed unless the ambiguity could invalidate the recommendation.

## Evidence discipline

Classify important claims, especially scale, latency, consistency, security, availability, recovery, cost, deployment,
and technology claims, as:

- **VERIFIED** - repository evidence, supplied measurement, authoritative documentation, or an observable fact.
- **USER-STATED** - explicit but not independently verified.
- **DERIVED** - logical consequence of evidence.
- **ASSUMED** - temporary input used to proceed.
- **UNKNOWN** - important information not available.
- **REQUIRES-MEASUREMENT** - cannot be responsibly settled by reasoning.

Use the compact ledger in [references/evidence-and-decisions.md](references/evidence-and-decisions.md). Unknowns do not
automatically block work: say whether each one changes the architecture, keeps a decision reversible, needs an
experiment, or can wait. Use ranges or scenarios instead of fake precision.

## Questions

Ask no more than five grouped, high-leverage questions for greenfield work. Ask only what could change the architecture,
prioritizing actors and critical workflows; invariants; scale and latency; data sensitivity, consistency, availability,
recovery, and offline/geographic needs; integrations, deployment, team, budget, operations, and deadline. If the user
wants a one-pass answer or cannot answer, proceed with explicit assumptions, sensitivity, reversible choices, and
validation steps.

## GREENFIELD workflow

Run these stages in order. Record only findings that affect a decision.

1. **Frame the product.** Extract purpose, actors, critical journeys, capabilities, business and technical invariants,
   external systems, damaging failure cases, and explicit non-goals. Separate requirements from implementation
   preferences. Challenge contradictions and premature technology demands.

2. **Define constraints.** Build a small constraint matrix covering expected adoption and growth horizon, peak/sustained
   rates, concurrency, latency, throughput, data volume/lifecycle/retention, transaction and consistency boundaries,
   availability, RTO/RPO, degraded and offline operation, geography, security/privacy/compliance, budget, team
   expertise, deployment ownership, on-call capacity, and delivery cadence. Use initial-validation, moderate-adoption,
   and plausible-growth scenarios when exact figures are unavailable. State whether the recommendation changes by
   scenario.

3. **Model ownership and risk.** Identify domain and capability boundaries, invariants and transaction/aggregate
   boundaries, data owners, trust zones, human/machine/service/admin identities, authorization rules, synchronous and
   asynchronous flows, critical paths, failure domains, scaling dimensions, deployment boundaries, external
   dependencies, and operational owners. Do not create components just to make a diagram tidy.

4. **Build the simple baseline first.** Start with one deployable application or the fewest justified deployables,
   explicit internal modules, clear boundaries, one primary data store unless evidence says otherwise, defined
   interfaces, proportional observability, and named future extraction points. Test the baseline against every material
   constraint; simplicity is a valid result, not a temporary apology.

5. **Compare credible alternatives.** Consider only serious candidates, such as modular monolith, independently
   deployable services, hybrid, selective asynchronous processing, local-first, serverless, plugin, or separate security
   zones. Compare fit, ownership, consistency, security, latency, throughput, reliability, failure behavior,
   deployment/debugging/testing, migration/rollback, cost, team fit, delivery risk, and reversibility. Explain rejected
   candidates and the evidence that rejected them.

6. **Run bounded review lenses.** Review domain/data, security/trust, performance/capacity, reliability/recovery,
   platform/operations, cost/organization, and migration/delivery. Each review must have a bounded question, relevant
   evidence, assumptions, and return findings, evidence, risks, disagreements, unknowns, and required validation. Use
   subagents only when available and useful; never require them and never replace synthesis with a best-practice dump.

7. **Run the adversary.** Try to remove services, databases, queues, hops, abstractions, and fashionable technology.
   Look for shared or circular ownership, distributed-monolith coupling, hidden synchronous dependencies, broken
   consistency assumptions, security gaps, missing failure behavior, operational mismatch, migration gaps, irreversible
   commitments, and documentation theatre. Preserve meaningful disagreement and reconcile it with evidence and
   constraints.

8. **Select.** Choose the simplest candidate that demonstrably satisfies the constraints. State why it wins, rejected
   alternatives, supporting evidence, assumptions, invalidation conditions, reversible decisions, expensive-to-reverse
   decisions, and decisions to delay. Security must influence topology and ownership before topology is finalized.

9. **Make it buildable.** Give implementation order: foundation and module/component sequence, contracts, data model,
   identity and authorization, observability, test strategy, deployment stages, benchmarks, migration, rollback, risk
   reduction, and measurable completion criteria. Add architecture fitness functions where practical: dependency/cycle
   rules, access and ownership checks, API/schema compatibility, secret/dependency policy, performance limits,
   timeout/retry/degraded-mode tests, resilience tests, deployment policy, or drift checks. Specify the rule and how it
   can actually be checked; do not claim an unenforced control exists.

## EVOLUTION workflow

Inspect the repository, build files, dependencies, modules, runtime/deployment configuration, tests, data paths,
architecture documents, ADRs, and failed approaches before recommending change. Recover declared architecture versus
repository reality, current boundaries and ownership, trust and deployment topology, and behavior that must remain
compatible.

Define the new capability and changed constraints, then compare extension, extraction, replacement, and hybrid
strategies. Prefer the safest staged path; do not propose a clean-room rewrite or microservices merely to make the
target look cleaner. Specify compatibility periods, contract evolution, data migration, rollout, rollback,
reconciliation, and fitness functions. State what can be preserved and what evidence justifies breaking it.

## REVIEW workflow

Inspect code and configuration rather than trusting diagrams. Find dependency cycles, hidden coupling, unclear or shared
ownership, shared mutable state/databases, inappropriate access, hidden trust boundaries, risky external connections,
distributed-monolith behavior, release coupling, drift, missing failure semantics or observability, and operational
ownership gaps. Rank findings by impact, likelihood, evidence strength, remediation cost, and urgency. Recommend the
safest useful evolution, not conformity to an idealized style.

## Mandatory gates

Apply [references/architecture-gates.md](references/architecture-gates.md) whenever the design contains a network
boundary, sensitive data, asynchronous work, or a material reliability/performance claim. In particular:

- Do not approve a separate service for conceptual distinctness alone. Require multiple strong signals such as
  materially different scaling, independent releases and stable ownership, privilege/failure/regulatory isolation,
  incompatible runtime/storage, or durable data ownership whose value exceeds coordination cost.
- Before a service boundary, name owner and data owner; identity, authorization, timeout/deadline, retry and idempotency
  behavior; duplicate/order handling; evolving contract; deployment/observability/debugging; and support owner. Reject
  shared databases, synchronized releases, circular dependencies, excessive synchronous calls, duplicate domain logic,
  weak isolation, or infrastructure cost without measurable value.
- Before topology is final, identify principals, assets and data classes, trust zones, authentication, authorization,
  delegation and revocation, tenant isolation, secrets, encryption, audit, supply-chain and abuse risks, compromise
  response, emergency controls, and secure degraded operation. Scale controls to risk; do not add enterprise machinery
  without need.
- For performance and reliability, model load, concurrency, hops, fan-out, storage operations, payloads, hot keys,
  contention, cache assumptions, timeouts, deadlines, retries, idempotency, duplicates, ordering, backpressure,
  overload, poison work, dependency/region failure, reconciliation, repair, degraded mode, RTO/RPO, and alerting. If not
  evidenced, specify a benchmark or experiment.
- Treat every important connection as a contract: caller, receiver, purpose, mode, identity, authorization, versioning,
  timeout, retry, idempotency, ordering, duplicates, partial failure, compatibility, observability, and recovery.

## Technology policy

Select in this order: derive required capabilities -> identify hard and soft constraints -> preserve an established
repository and team ecosystem where reasonable -> compare credible patterns -> compare candidate technologies -> reject
candidates that violate constraints -> compare delivery, operational, security, performance, compatibility, and
reversibility consequences -> choose the simplest sufficient option -> record what evidence would invalidate the choice.
Do not fill missing product context with a conventional web stack; if a framework, database, hosting model, UI
technology, language, or deployment model is not driven by evidence, leave it open or mark it as an explicit assumption
and explain its reversibility.

When candidates are materially equivalent, prefer in this order: the existing repository ecosystem; the team's
demonstrated expertise; lower operational burden; stronger standard-library or platform support; easier testing and
observability; easier rollback and migration; and lower supply-chain and dependency risk. Do not use a maintainer's
personal preference as a hidden tie-breaker.

Do not treat unfamiliarity as evidence that a technology is unsuitable, force a language because it is fashionable,
change an established repository language without material justification, or pretend two technologies are equivalent
when ecosystem or team evidence says otherwise. Verify current or version-sensitive claims from authoritative sources
before treating them as facts.

Do not create a custom orchestrator, database, server, broker, runtime, UI, CLI, or helper tooling for this skill. It is
instruction-only in v0.

## Output

Adapt depth to risk. Use [references/architecture-output-template.md](references/architecture-output-template.md) as a
menu, not a checklist. Every final architecture response must clearly identify selected mode and architecture, simplest
baseline, evidence/assumptions/unknowns/measurements, boundaries and ownership, security and failure behavior, rejected
alternatives and trade-offs, invalidation conditions, implementation order, migration/rollback where relevant, and
practical fitness functions. Add diagrams only when they communicate ownership, trust, runtime, connection, failure, or
deployment consequences.

End with unresolved questions and validation actions. If the request is too small for architecture analysis, say so and
handle the task directly.
