# 双 backend 执行体系架构与多态评审

Status: design-critic approved final revision with no blocking issues (2026-10-20). No code change in this batch.

> **Superseded 2026-10-21** → [execution-architecture.md](execution-architecture.md) is the single execution authority; this file is history only (conflicts: merged doc wins).

## 1. Scope

Cover 43499 kt-step and 43284 page-cache, plus the shared execution/step/access layers.

Current execution pieces:

- core.engine.ExecutionEngine: state machine interface; only ScriptedEngine exists.
- core.steps.StepEngine: generic order/retry/freeze; calls OpsInvoker.
- core.steps.StepPlans: one when over all StepId values (W1/W2/W3 + Patch1/Carrier/Hook/Trigger/Wait).
- core.steps.PlanCatalog: both backend step lists in one object.
- core.steps.StepInputs/StepId: one type for both backends.
- core.access.AccessCapability: KernelMemoryAccess/PageCacheAccess interfaces.
- core.AccessSession and core.access.AccessSession: two session contracts.
- backend.cve43499 KtStepEngine/KtOpsInvoker; backend.cve43284 PageCacheEngine/Stage pipeline.

## 2. Findings

1. Step abstraction is a closed enum plus a single when, not polymorphism. Adding a backend/step edits core files. This is simple and testable, but violates backend isolation.
2. Access capabilities already use interface segregation, but there are two AccessSession contracts and a broad AccessCapability marker; ownership/lifecycle is ambiguous.
3. ExecutionEngine is a placeholder; UI-facing state machine and backend execution are not separated.
4. 43284 has no backend package/target ownership; PageCacheEngine/stages live under engine, jni under jni.cve43284, plan under core.steps.
5. C++ KernelAccess/KernelAccess43499 is a virtual access hierarchy; route-specific landing is separate. It is legacy for kt-step and should not define the new architecture.

## 3. Polymorphism principles

Use polymorphism only at stable boundaries:

- Access capabilities: interface per capability, implemented by backend sessions.
- OpsInvoker: one call boundary between StepEngine and backend.
- BackendRun/ExecutionBackend: one high-level run boundary per backend.
- Stage: 43284 page-cache stage pipeline.
- C++ WordLanding/route landing: only where route mechanics differ.

Do not use inheritance for:

- steps/plans (they are data, not objects);
- params/results (data classes);
- backend sessions (composition of capabilities, not a base class hierarchy);
- route parameter differences (use data/strategy, not subclass per route).

Virtual dispatch cost is negligible at session/step boundaries (one call per step/op), but not acceptable in PI hot paths; no virtual per futex/syscall.

## 4. Access capability decision

Keep:

- one lifecycle contract: AccessSession with phase/cleanup/close;
- AccessCapabilities as composition: kernelMemory?, pageCache?;
- KernelMemoryAccess and PageCacheAccess as independent interfaces extending only AccessCapability marker;
- backend sessions implement capabilities by composition; no AbstractBackendAccess.

Canonical session: keep one interface in `core.access`:

    interface AccessSession : AutoCloseable {
        val phase: SessionPhase
        val cleanup: CleanupDisposition
        val capabilities: AccessCapabilities
        suspend fun initialize(): AccessResult<InitializationReceipt>
        suspend fun snapshot(): AccessResult<SessionSnapshot>
        suspend fun shutdown(): AccessResult<ShutdownReceipt>
        override fun close() // cancel-only/best-effort; does NOT free native handle; shutdown() required
    }

- remove `core.AccessSession.kt`; migrate its callers/tests to `core.access.AccessSession`;
- capability views are non-owning; they never close; generation invalidates old views;
- lifecycle: add `SessionPhase.CLOSING`; `close()` is non-blocking cancel-only: sets CLOSING, signals cancellation, does NOT await and does NOT free the native handle. Suspend `shutdown()` awaits the active call, performs native cleanup, and sets CLOSED; idempotent. There is no close-then-CLOSED claim. DIRTY never returns READY.
- serialization fields: `stateLock: ReentrantLock`, `activeJob: Job?`, `phase`, `shutdownMutex: Mutex`. Both close and admission use the same `stateLock.lock()/unlock()` (no synchronized, no second mutex): close locks, if phase in CLOSING/CLOSED return, sets CLOSING, cancels activeJob, unlocks; admission locks, refuses phase != READY, registers activeJob, unlocks, calls native, finally locks and clears self-only when phase != CLOSED. No admission can register after CLOSING.
- shutdown(): if CLOSED return receipt; if != CLOSING close(); `shutdownMutex.withLock { if CLOSED return; job=stateLock.lock{activeJob}; job?.join(); stateLock.lock{if CLOSED return; activeJob=null; phase=CLOSED}; nativeShutdown() }`. Repeated shutdown no native cleanup. Tests: close-then-shutdown, shutdown-then-close, close-in-flight, capability-after-close.
- File/type migration:
  - delete `core/AccessSession.kt`; move `SessionPhase`/`CleanupDisposition` to `core/access/AccessSession.kt`;
  - move `core/AccessSessionFactory.kt` + `AccessSessionRequest` to `core/access/AccessSessionFactory.kt`; factory returns canonical `core.access.AccessSession`;
  - implementations: `core/access/LkmKernelAccess.kt`, `data/nativeaccess/JniAccessSession.kt`, `JniCapabilitySession.kt`, `JniCapabilitySessionFactory.kt` implement initialize/snapshot/shutdown/close; capability views remain non-owning;
  - fake: `FakeAccessSession` in host test source set.
- Callers/tests migrated: `AccessSessionFactory` tests, LKM/JNI session lifecycle tests, UI adapters; add close-then-shutdown, shutdown-then-close, capability-after-close, DIRTY-never-READY. Host link: core + fakes only.

C++:

- KernelAccess/KernelAccess43499 is legacy; do not add new backends to it.
- New kt-step uses Native* ports, not KernelAccess.
- 43284 native helper/raw_esp stay in cve43284_core; no shared access base.

## 5. Steps decision: no generic Step parent class

Recommendation: do NOT create Step base class inheritance.

Keep StepEngine generic over a backend input type. Replace the monolith with data + per-backend catalogs:

    data class StepSpec(val backendId: String, val key: String, val token: String)
    interface StepCatalog<I> {
        val backendId: String
        fun steps(): List<StepSpec>
        fun select(tokens: List<String>): Pair<List<StepSpec>, String?>
        fun plan(step: StepSpec, inputs: I): StepPlan
    }
    data class StepResult(val step: StepSpec, val outcome: StepOutcome, val reason: String?, val calls: List<String>, val details: Map<String, String> = emptyMap())
    class StepEngine<I>(private val catalog: StepCatalog<I>, private val ops: OpsInvoker) {
        fun run(selected: List<StepSpec>, inputs: I): List<StepResult>
    }

Cve43499StepCatalog implements StepCatalog<Cve43499StepInputs>; it owns W1/W2/W3 order and token selection. 43284 does not use StepEngine (see §6). StepSpec.key is opaque; no core StepId union. StepResult carries StepSpec, not StepId. Remove per-backend fields from core StepInputs/StepPlans/PlanCatalog only after both replacements exist.

If a step later needs custom logic, use a StepHandler function/interface in the catalog (composition), not subclassing.

Benefits: no Lifecycle/state in step objects; easy table tests; backend packages own their steps. Cost: one indirection at plan lookup, negligible.

## 6. Execution boundary

Introduce one high-level interface:

    fun interface Progress { fun onStep(name: String, index: Int, total: Int) }
    interface BackendRun : AutoCloseable {
        val backendId: String
        suspend fun run(request: BackendRunRequest, progress: Progress = {}): BackendRunResult
        fun stop()       // non-suspend cancel/abort; safe in a cancelled coroutine
        override fun close() // non-suspend; cancel and release, no await
        suspend fun shutdown() // await native cleanup; called outside cancellation
    }

ExecutionEngine changes to `interface ExecutionEngine : AutoCloseable`, with `override fun close()` idempotent, no concurrent run; UI closes every factory-created engine. ScriptedEngine and every fake/execution implementation migrate: close sets `Failed("engine-closed")` once, second close no-op, run after close rejected. Add a compile/test guard enumerating all implementations.

Implementations:

- Cve43499KtStepRun (wraps Cve43499RunEngine)
- Cve43284PageCacheRun (wraps PageCacheEngine/Stage pipeline)

Concrete boundary:

    data class BackendRunRequest(
        val backendId: String,
        val pathId: String,
        val values: Map<String, Any>,
        val profile: ByteArray?,
        val expectedUid: Long?,
    )
    sealed interface BackendRunResult {
        data class Ok(val note: String, val uid: Long?, val steps: List<StepResult>)
        data class Failed(val reason: String, val steps: List<StepResult>)
    }
    class DelegatingExecutionEngine(
        private val run: BackendRun,
        private val log: (String) -> Unit = {},
    ) : ExecutionEngine {
        // state transitions: Checking -> RunningPath -> Finishing -> Success/Failed
        // maps Plan -> BackendRunRequest; cancellation/close propagates to run
    }

`Plan` maps via `path.id` + `values` + profile frame; backend-specific config is built inside each BackendRun.

Concrete engine:

    class DelegatingExecutionEngine(
        private val run: BackendRun,
        private val log: (String) -> Unit = {},
    ) : ExecutionEngine, AutoCloseable {
        private val _state = MutableStateFlow<ExecutionState>(Idle)
        private val closed = AtomicBoolean(false)
        private val used = AtomicBoolean(false)
        private val running = AtomicBoolean(false)
        override fun close() {
            if (closed.get()) return
            if (!running.compareAndSet(false, true)) throw IllegalStateException("run in progress")
            if (!closed.compareAndSet(false, true)) { running.set(false); return }
            try { run.close() } finally { running.set(false); _state.value = Failed("engine-closed") }
        }
        override val state: StateFlow<ExecutionState> = _state
        override suspend fun run(plan: Plan) {
            check(!closed.get() && used.compareAndSet(false, true) && running.compareAndSet(false, true)) { "engine closed/used/busy" }
            try {
            val request = BackendRunRequest(
                backendId = run.backendId,
                pathId = plan.path.id,
                values = plan.values,
                profile = null,
                expectedUid = null,
            )
            _state.value = Checking(run.backendId)
            _state.value = RunningPath(request.pathId, 0f)
            try {
                val result = run.run(request) { name, index, total ->
                    _state.value = RunningPath(name, (index + 1f) / total)
                }
                _state.value = Finishing(request.pathId)
                _state.value = when (result) {
                    is BackendRunResult.Ok -> Success(result.note)
                    is BackendRunResult.Failed -> Failed(result.reason)
                }
            } catch (c: CancellationException) {
                run.stop()
                _state.value = Failed("cancelled")
                throw c
            } catch (t: Throwable) {
                _state.value = Failed(t.message ?: t.javaClass.simpleName)
                throw t
            }
            } finally { running.set(false) }
        }
    }

BackendRun exposes `suspend fun run(request, onStep): BackendRunResult`; cancellation stops native work then rethrows. Ownership: DelegatingExecutionEngine implements AutoCloseable and owns the injected BackendRun; engine.close() calls run.close() exactly once after no run is active; run() itself never closes. All BackendRun implementations are one-shot (including Cve43499KtStepRun and Cve43284PageCacheRun); DelegatingExecutionEngine is one-shot (`used` flag) and rejects a second run. UI obtains a fresh engine from `ExecutionEngineFactory`:

    interface BackendRunFactory { val backendId: String; fun create(): BackendRun }
    class ExecutionEngineFactory(private val factories: Map<String, BackendRunFactory>) {
        init { require(factories.all { (key, f) -> key == f.backendId }); require(factories.size == factories.keys.distinct().size) }
        fun create(backendId: String): ExecutionEngine? {
            val f = factories[backendId] ?: return null
            require(f.backendId == backendId)
            return DelegatingExecutionEngine(f.create())
        }
    }

ExecutionEngine extends AutoCloseable: `override fun close()` is idempotent and rejects concurrent run; factory-created engine must be closed by UI (use/try-finally). Factory is reusable/concurrent; each create() returns a fresh one-shot engine. Tests: create+close 43499 and 43284, two concurrent engines, close after success/failure/cancel.

No concurrent run/close. Tests: success->second run rejected, failure->second run rejected, cancel->second run rejected, close-after-complete, factory two-execution 43284. Fake tests close after success, failure, cancellation. 43284 aggregation: Cve43284PageCacheRun calls StagePageCacheEngine.run() and emits one StepResult per StageResult in returned order (Environment, Helper, Module, Hook, Trigger, Wait, Cleanup). StepSpec token = stage name lower-case, key = index+name. Cleanup result is always emitted as its own StepResult, even after an earlier failure; a cleanup RESTORE_FAILED overrides any earlier OK/Frozen and makes the overall result Failed. Overall BackendRunResult.Ok only if every StageResult (including cleanup) is OK; otherwise Failed(reason); partial/restore individual results stay Frozen. Cancellation still emits cleanup before rethrow. CANCELLED rethrows after cleanup. Mapping: OK->Ran; ENV_MISSING/TRIGGER_FAILED/WAIT_TIMEOUT->Refused; HELPER_VERIFY/MODULE_PARTIAL/HOOK_PARTIAL/RESTORE_FAILED->Frozen; CANCELLED->Refused then rethrow; reason/evidence copied into StepResult.details.

Stage contract change: `Stage.abort(code)` and `Stage.close()` become **non-suspend** cleanup primitives; only `Stage.run()` stays suspend. StagePageCacheEngine.abort/close are non-suspend; Cve43284PageCacheRun.stop/close can call them.

StageRunner signature includes `observer: StageObserver = NoopStageObserver`. Exact flow:

    val out = mutableListOf<StageOutcome>()
    var pending: CancellationException? = null
    var primary: Throwable? = null
    try {
        for ((i, stage) in stages.withIndex()) {
            observer.onStageStart(i, stage)
            val r = try { stage.run() } finally { observer.onStageEnd(i, stage) }
            out += StageOutcome(keys[i], r)
            if (r.code != OK) { stage.abort(r.code); break }
        }
    } catch (c: CancellationException) {
        pending = c
    } catch (t: Throwable) {
        primary = t
        out += StageOutcome("cancelled", StageResult(CANCELLED, t.javaClass.simpleName))
    } finally {
        val cleanupResult = cleanup?.let { runCatching { it.run() }.getOrElse { StageResult(RESTORE_FAILED, "cleanup-crash") } }
        if (cleanupResult != null) out += StageOutcome("cleanup", cleanupResult)
        // Stage close is owned by StagePageCacheEngine, not StageRunner.
    }
    if (pending != null) throw pending
    return StageRunResult(out, primary)

StageRunResult(outcomes, primary); primary is returned, not thrown. Cve43284PageCacheRun maps outcomes to StepResults and primary to BackendRunResult.Failed(reason, steps); only CancellationException is rethrown. Cleanup always appends one result; reverse close always runs; cancellation precedence is preserved. StageOutcome(key,result) keys environment/helper/module/hook/trigger/wait/cleanup; cleanup last. StageRunner takes an explicit `StageObserver` (`onStageStart(index, stage)`, `onStageEnd(index, stage)`), implemented by StagePageCacheEngine. Engine keeps `executed/current` under `stageLock`; observer registers before run and clears after. abort(code) takes stageLock, aborts only executed/current (not-yet-run no-op); close() non-suspend takes stageLock, sets closed, cancels active run, closes executed stages once without awaiting; shutdown() suspend awaits the job before final cleanup. StagePageCacheEngine is single-run and the sole close owner; StageRunner never calls stage.close(). Engine run() wraps StageRunner in try/finally and **always** closes executed stages exactly once on normal completion, failure, or cancel; after run it marks closed and rejects a second run. stop() = cancel job + abort(CANCELLED), no close. close() = set closed/closeRequested, cancel job, no await; if run active, run-finally closes; if completed, close executed now. shutdown() joins then closes once (idempotent). Cve43284PageCacheRun is one-shot; BackendRun reuse requires a new run object. Tests: normal success closes once, failure closes once, cancel closes once, close-after-complete idempotent, second run rejected. Cve stop->abort->close uses this ordering; no concurrent run/close. Cve43284PageCacheRun.stop() non-suspend: cancel active job, abort(CANCELLED), no close. close() non-suspend: cancel, abort, close stages exactly once. shutdown() suspend: join job, then native/cleanup. Engine cancellation calls stop() synchronously then rethrows; engine.close calls run.close(). Test: cancel during Wait -> one cleanup result key cleanup, same CancellationException identity rethrown, no stage open. Fake Cve43499FakeRun/Cve43284FakeRun host-only; UI imports core.engine plus existing allowlist.

## 7. 43284 structure

    backend.cve43284
      pagecache/PageCacheEngine, StageConfigProvider, elf/, lkm/, stage/, write/
      native/PageCacheInit43284, JniPageCacheOps
      # no StepCatalog: 43284 keeps Stage pipeline; BackendRun wraps PageCacheEngine
      # jni.cve43284 package/ABI stays unchanged; no 43284 JNI package move

C++: support_43284_core (helper) stays; raw_esp/ghostlock43284 remain the external 43284 artifact, not owned by this plan; independent of gl43499native/glgeneral.

## 8. Migration phases

1. Merge access session contracts; freeze capability interfaces; host tests.
2. Move 43499 to backend.cve43499.* and 43284 to backend.cve43284.* (approved package plan). jni.cve43284 package/ABI unchanged; ghostlock43284 stays external.
3. Add Cve43499StepCatalog/StepEngine<I>; remove 43499 cases from core StepPlans/PlanCatalog/StepInputs. 43284 keeps Stage pipeline; add Cve43284PlanAlias resolving pagecache_write -> fixed Stage sequence.
4. Add BackendRun + DelegatingExecutionEngine + fake 43499/43284 runs; UI allowlist includes core.engine; host tests.
5. Move 43284 pagecache Kotlin to backend.cve43284 (if not phase 2); no 43284 native target/ABI change. AVD uses existing ghostlock43284.
6. Remove legacy KernelAccess from production native; keep only legacy target.

## 9. Tests/gates

- StepCatalog per backend: order/unknown/missing/retry/freeze tests.
- Access: one session contract test; capability-null and generation tests; no close on capability.
- BackendRun: 43499/43284 fake runs through ExecutionEngine (host-only), state/progress/cancel tests; UI imports core.engine + backend.common/core.model and existing cve43284.pagecache exceptions.
- 43284 alias: profile token pagecache_write is the single entry; Cve43284PlanAlias resolves it to the actual StageFactory sequence Environment, Helper, Module, Hook, Trigger, Wait, Cleanup. Cve43284PageCacheRun uses StageFactory/StagePageCacheEngine directly; the alias validates the token only. Unknown token refuses. Test asserts alias stage order equals StageFactory/StagePageCacheEngine order.
- Phase link state: phases 1-4 host-only; phase 5 AVD loads existing ghostlock43284; no new 43284 native target.
- 43284 target: JNI load/nm; independent of 43499.
- No virtual/inheritance added to step data; dependency scan enforces core.steps has no backend cases.
