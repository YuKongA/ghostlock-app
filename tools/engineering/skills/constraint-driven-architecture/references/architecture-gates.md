# Architecture gates

Use only the gates relevant to the proposed shape. A gate is passed by explicit evidence or by a stated validation plan; generic best practices do not pass it.

## Network/service boundary gate

Require multiple strong signals: materially different scaling, independent release cadence, stable distinct ownership, meaningful privilege or failure isolation, incompatible runtime/storage, regulatory isolation, or durable domain/data ownership. The benefit must exceed network, coordination, deployment, debugging, and on-call cost.

Before approval, answer:

- Who owns the component and its data?
- Which identity calls it, with what authorization and secret handling?
- What are timeout, deadline, retry, idempotency, duplicate, and ordering rules?
- How do contracts evolve and remain compatible?
- How is it deployed, observed, debugged, rolled back, and supported?

Reject a boundary that requires a shared database, synchronized releases, circular calls, excessive synchronous fan-out, duplicate domain logic, unclear ownership, weak isolation, or unmeasured infrastructure cost.

## Security gate

Name principals, assets, data classifications, trust zones, authentication, authorization, delegation, revocation, tenant isolation, secrets, encryption, audit, supply-chain risks, abuse cases, privilege escalation, compromised-node behavior, emergency controls, and secure degraded operation. Every important component needs an owner, trust assumptions, least-privilege access, secrets handling, audit responsibility, and compromise response.

## Performance gate

State peak and sustained load, concurrency, critical-path latency, hops, fan-out, storage operations, payload size, CPU, memory, I/O, hot keys, contention, and cache assumptions. Attach a representative benchmark when the target or bottleneck is not evidenced.

## Reliability gate

Define deadlines, timeouts, retry safety, idempotency, duplicate and out-of-order handling, backpressure, overload behavior, queue growth and poison work, dependency and regional failure, reconciliation and repair, degraded mode, RTO, RPO, and alerting. Specify the smallest failure test that can falsify the design.

## Contract gate

For each important synchronous or asynchronous connection record caller, receiver, purpose, identity, authorization, contract/schema, versioning, timeout, retry, idempotency, ordering, duplicate handling, partial failure, compatibility, observability, and recovery. Ask what happens when the interaction is slow, unavailable, duplicated, reordered, rejected, or partially complete.
