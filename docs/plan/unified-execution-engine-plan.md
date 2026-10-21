# Unified execution engine plan (Step + Stage)

Status: design-critic approved revision 6 with no blocking issues (2026-10-20). Supersedes dual-authority parts of backends-execution-architecture-plan.md sections 5/6. No code change.

> **Superseded 2026-10-21** — merged into [execution-architecture.md](execution-architecture.md), now the single
> execution authority (PipelineEngine semantics, KtStep/Stage as node forms, AccessSession-vs-execution close exclusion).
> Kept as history only; on any conflict the merged document wins. No code change by this banner.

## 1. Decision

One execution authority: PipelineEngine. StepEngine and StageRunner are deleted as execution authorities.

- StepCatalog/StepPlans become OpNode builders.
- StageFactory becomes StageNode builder.
- BackendRun owns PipelineEngine and maps PipelineResult to BackendRunResult.
- Node policies are per-node; there is no global attempts policy.

## 2. Core types

~~~
data class RunPolicy(
  val attempts: Int = 1,
  val settleMs: Long = 0,
  val skipPolicy: SkipPolicy = SkipPolicy.None,
)

enum class SkipPolicy { None, FrozenRemaining }

sealed interface RunNodeResult {
  data class Done(val evidence: Map<String,String> = emptyMap()) : RunNodeResult
  data class Retryable(val reason: String) : RunNodeResult
  data class Refused(val reason: String, val evidence: Map<String,String> = emptyMap()) : RunNodeResult
  data class Frozen(val reason: String, val evidence: Map<String,String> = emptyMap()) : RunNodeResult
}

interface RunNode {
  val backendId: String
  val key: String
  val token: String
  val policy: RunPolicy
  suspend fun run(): RunNodeResult
  fun calls(): List<String> = emptyList()
  fun abort(reason: String)
  fun close()
}

data class NodeDescriptor(val backendId: String, val key: String, val token: String)
data class NodeOutcome(val node: NodeDescriptor, val result: RunNodeResult, val calls: List<String> = emptyList())

enum class Overall { Ok, Failed }
data class PipelineResult(
  val outcomes: List<NodeOutcome>,
  val primary: RunNodeResult?,
  val cleanup: NodeOutcome?,
  val overall: Overall,
)
~~~

## 3. Pipeline semantics

- Each node has its own RunPolicy. 43499 uses StepInputs.attempts/settleMs; 43284 uses attempts=1.
- Order: nodes run in list order. Retryable retries only its own node. Exhausted retries convert to Refused(reason, evidence) with calls from all attempts, no further native call; primary/overall=Failed.
- Refused stops; Frozen stops and prevents further native calls.
- Stop parity: Refused never emits remaining nodes, regardless of skipPolicy. Only Frozen consults the failing node's skipPolicy: FrozenRemaining appends Frozen("engine-frozen") to every remaining node; None emits none. 43499 Frozen uses FrozenRemaining; 43284 uses None. Mixed-policy test includes a 43499 Refused node with FrozenRemaining and expects no remaining entries.
- Non-cancellation throwable from RunNode.run becomes NodeOutcome(descriptor, Frozen(reason, evidence), node.calls()); abort is called before cleanup. Throwable from abort/cleanup is recorded but does not replace primary. A throw after partial calls keeps calls() snapshot; before calls empty.
- CancellationException is never converted to a result: PipelineEngine aborts the current node, runs cleanup, closes nodes in reverse, then rethrows the original exception. No NodeOutcome for the cancelled node.
- Cleanup is a separate engine-owned RunNode; it runs exactly once in a NonCancellable finally, never retries (policy ignored), is not aborted, and its result is appended. PipelineEngine.close closes main nodes reverse, then cleanup, exactly once.
- Overall/primary table: Ok iff all main outcomes and cleanup are Done. Otherwise Failed. primary = first non-Done main outcome; cleanup non-Done overrides primary with Frozen(cleanup); synthetic engine-frozen remaining nodes never become primary. Refused/Frozen main outcomes set Failed. Cancellation rethrows and has no PipelineResult.
- Close: PipelineEngine owns nodes, reverse closes exactly once, idempotent; no concurrent run/close; run-after-close rejected.
- abort(reason) targets only `current` node under a lock; abort exceptions are recorded/logged but do not replace the primary failure.

## 4. Node adapters

43499: Cve43499OpNode(descriptor, StepPlan, OpsInvoker, policy).
- StepPlan.Missing -> Refused("precondition:<token>-<input>") with calls=emptyList and no OpsInvoker call; catalog does not reject at build time, node returns the refusal.
- StepPlan.Ready(emptyList()) -> Refused("precondition:<token>-no-calls") with no OpsInvoker call.
- StepPlan.Ready invokes calls in order; ok->Done; not-ready->Retryable; indeterminate->Frozen; failed->Refused.
- abort/close no-op. skipPolicy=FrozenRemaining.

43284: Cve43284StageNode(Stage, key, token).
- run: stage.run(); OK->Done; ENV_MISSING/TRIGGER_FAILED/WAIT_TIMEOUT->Refused; HELPER_VERIFY/MODULE_PARTIAL/HOOK_PARTIAL/RESTORE_FAILED->Frozen; returned StageCode.CANCELLED->Frozen("stage-cancelled"); thrown CancellationException always propagates and is never mapped.
- abort/close delegate to Stage; only PipelineEngine calls them. Stage.close is non-suspend cleanup; Stage.run remains suspend. skipPolicy=None.

StageCode mapping is total; evidence is copied into NodeOutcome.result.evidence.

## 5. BackendRun boundary

~~~
sealed interface BackendRunResult {
  data class Ok(val note: String, val uid: Long?, val steps: List<NodeOutcome>) : BackendRunResult
  data class Failed(val reason: String, val steps: List<NodeOutcome>) : BackendRunResult
}
~~~

NodeOutcome contains only NodeDescriptor/result/calls, not RunNode. StepResult/StageResult are internal evidence and never cross BackendRun. Cve43499Run and Cve43284Run call PipelineEngine and map PipelineResult.overall/primary to BackendRunResult.

## 6. Deletions

- delete StepEngine;
- delete StageRunner;
- StepPlans remains a translator;
- StagePageCacheEngine is deleted; StageFactory returns StageNodes(nodes, cleanup) as pure data. PipelineEngine/Cve43284StageNode own close; close counter exactly once.
- StepResult/StageResult are internal only.

## 7. Migration phases

1. **DONE (host, 2026-10-20)**: PipelineEngine + RunNode/PipelineResult + fakes/tests in core.engine; no production switch yet. 8 tests: order/close, retry, skip policy, cleanup override, cancellation identity, thrown node/abort, cleanup close, run-after-close.
2. **DONE (host, 2026-10-20)**: Cve43499OpNode + Cve43499StepCatalog + Cve43499Run implemented; KtStepEngine delegates to Cve43499Run/BackendRun/PipelineEngine; KtStepEngineTest 13/13. KernelMemoryEngine now uses Cve43499StepCatalog+PipelineEngine; StepEngine/PlanCatalog deleted with StepEngineTest.
3. **DONE (host, 2026-10-20)**: Stage.abort/close non-suspend; Cve43284StageNode/StageFactory.stages/Cve43284Run implemented; PageCacheEngine.runStages uses Cve43284Run(config).runStages(); StageRunner and StagePageCacheEngine deleted with their tests. Mapping/order/cleanup tests pass; AVD Cve43284PipelineInstrumentedTest (fake stages) PASS.
4. **DONE (host, 2026-10-20)**: BackendRun/BackendRunResult/Progress, DelegatingExecutionEngine, ExecutionEngineFactory, ScriptedEngine close, Cve43499BackendRun/Factory tests implemented. KtStepEngine now calls DelegatingExecutionEngine.execute(Cve43499BackendRun); sync run() is a compatibility wrapper over suspend runSuspend(). Cve43499Run/BackendRun expose non-suspend stop() -> PipelineEngine.abort, and BackendRun.close calls stop. AVD `KtStepPipelineInstrumentedTest` (fake route/probe, no native) PASS. No UI switch.
5. **DONE (host, 2026-10-20)**: canonical core.access.AccessSession extends NativeSession; LkmGlkSession/PageCacheKernelAccess implement lifecycle + capabilities; JniCapabilitySession implements only canonical; transport contract renamed to NativeSession/NativeSessionFactory and JniAccessSession is transport-only. Contract test passes. Dual result types still to remove.
6. **PARTIAL (host, 2026-10-20)**: NativeAttemptKernelRoute maps NativeAttemptResult to Landed/Refused/Indeterminate with Kotlin criteria; fake-port tests pass. Cve43499PreparedRoute builds the template from NativePrepareResult.Ok + KernelFutexWords + KtStepPlan (pageFd=shapingFd); Cve43499ProductionEngine exposes run(plan) over that route + KtStepEngine. AVD Cve43499ProductionRouteInstrumentedTest PASS: real prepare/attempt, fixture probe, FixtureSucceeded; gate archived docs/analysis/device-gates/cve43499-route-avd-20261020-1619.md. No caller/UI switch; no uid=0 claim.
7. **DONE (host/AVD lifecycle, 2026-10-20)**: Cve43499ProductionFlow owns ProcessVictimSession; VictimStartResult/startDetailed; ProcessVictim atomic construction + bounded close; session probe timeout/close; flow host tests; AVD ProcessVictimInstrumentedTest (real /system/bin/sh) PASS. No uid=0 claim.
8. **DONE (AVD composition, 2026-10-20)**: Cve43499ProductionFlow + real ProcessVictimSession + Cve43499PreparedRoute + KtStepEngine PASS on Pixel_5 API33; expectedUid is set to the shell's actual uid, so Succeeded is lifecycle parity, not privilege escalation. Gate: docs/analysis/device-gates/cve43499-flow-avd-20261020-1624.md.
9. **PARTIAL (AVD lifecycle, 2026-10-20)**: Cve43499ProductionSession.open owns syscalls/prepare/words/attempt/flow; failure paths close resources; close is reverse-order/idempotent. AVD Cve43499ProductionSessionInstrumentedTest open+close PASS. No caller/UI switch; no uid=0.
10. **DONE (host API, 2026-10-20)**: Cve43499ProductionRunner.run(params, plan) is the one-shot production entry (open -> run -> close, session failure normalized). No UI/config caller yet.

## 8. Gates

- 43499 parity: order/retry/refused/frozen/skipPolicy.
- 43284 parity: order/partial/freeze/cleanup/restore/cancel/no skipped.
- Cancellation: original identity through StageNode->PipelineEngine->BackendRun; cleanup once; close reverse once.
- Node exception, abort exception, cleanup exception, ops exception -> Frozen/Failed.
- Run-after-close rejected; second run rejected; close counter exactly once.
- No StepEngine/StageRunner symbols in production after phase 5.
