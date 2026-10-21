# Backend access + execution design

Status: design-critic approved revision 7 with no blocking issues (2026-10-20). Supersedes conflicting parts of the earlier dual-backend and unified-engine drafts. No code change.

> **Superseded 2026-10-21** — merged into [execution-architecture.md](execution-architecture.md), now the single
> execution authority (PipelineEngine semantics, KtStep/Stage as node forms, AccessSession-vs-execution close exclusion).
> Kept as history only; on any conflict the merged document wins. No code change by this banner.

## 1. Decisions

1. AccessSession owns capability/session JNI handles only. BackendRun/PipelineEngine owns execution node/port/Stage handles. The two close paths are disjoint and each closes exactly once.
2. KernelMemoryAccess/PageCacheAccess are capability interfaces implemented per backend by composition; no concrete capability base class.
3. Capabilities are non-owning views; absent capability is null, not a dummy.
4. Read/write primitives use capabilities; discover/prepare/route/actor/victim/stage/LKM use backend-specific JNI/ports.
5. PipelineEngine is the only execution authority; StepEngine and StageRunner are deleted.
6. BackendRun is one-shot and maps PipelineResult to BackendRunResult.
7. UI sees only ExecutionEngine/ExecutionState/ExecutionEngineFactory and backend public entry points.

## 2. Layers

~~~
UI / service
  -> ExecutionEngine (state machine, one-shot, closable)
     -> BackendRun (one per execution)
        -> PipelineEngine (order/retry/freeze/cancel/cleanup/close)
           -> RunNode (OpNode for 43499, StageNode for 43284)
              -> backend port / JNI

External read/write path:
AccessSession -> AccessCapabilities -> KernelMemoryAccess/PageCacheAccess -> backend JNI
~~~

Capabilities are not used by PipelineEngine nodes; nodes use backend ports directly.

## 3. AccessSession and capabilities

~~~
interface AccessSession : AutoCloseable {
  val phase: SessionPhase
  val cleanup: CleanupDisposition
  val capabilities: AccessCapabilities
  suspend fun initialize(): AccessResult<InitializationReceipt>
  suspend fun snapshot(): AccessResult<SessionSnapshot>
  suspend fun shutdown(): AccessResult<ShutdownReceipt>
  override fun close()
}

interface AccessCapabilities {
  val kernelMemory: KernelMemoryAccess?
  val pageCache: PageCacheAccess?
}
~~~

KernelMemoryAccess and PageCacheAccess remain independent interfaces extending only the AccessCapability marker. They expose read/write/targets/writeImage and descriptors; no close.

Per backend:
- No Cve43499Session in the access factory. 43499 has no generic kernel read/write capability (process_vm_* targets user memory); its prepare/route/actor ports are owned by Cve43499Run/PipelineEngine, not an AccessSession.
- Cve43284Session wraps JniAccessSession + PageCacheKernelAccess(JniPageCacheOps(JniAccessSession, target)); JniCapabilitySession is deleted. target = PageCacheTarget(id=init.carrierPath, path=init.carrierPath, writable=true); role=PageCacheRole.Carrier(2). PageCacheKernelAccess : PageCacheAccess has descriptor/targets/read/write/writeImage plus internal initialize()/snapshot()/shutdown()/lastReason(); it is not AccessSession. targets() = JniPageCacheOps.targets; read(offset,16)=ReadBlock(token,offset,16); write(offset,16)=WriteBlock(token,offset,16); writeImage=default 16-byte loop; up()=Snapshot page-cache flag + ResolveTarget. Cve43284Session.initialize = page.initialize() -> InitializationReceipt([pageCache descriptor]); phase READY on success/FAILED on failure; snapshot = page.snapshot(); close non-suspend cancel-only; shutdown = page.shutdown()->ops.tearDown()->jni.close() exactly once -> CLOSED. PageCacheKernelAccess exposes generation/ready/lastReason. LkmSession delegates initialize/snapshot/shutdown to LkmKernelAccess, owns fd once, phase/cleanup from it; kernelMemory view only. kernelMemory=null for 43284.
- LkmSession (LKM tier, not one of the two CVE backends) is the current KernelMemoryAccess implementation; it exposes kernelMemory and pageCache=null.
- If a future backend supports both, it composes both views over one session handle.

Lifecycle:
- stateLock: ReentrantLock protects phase + activeJob; admission and close use the same lock.
- close() non-suspend: set CLOSING, cancel activeJob, no await; does not free native handle.
- shutdown() suspend: join activeJob, native cleanup, set CLOSED; idempotent; repeated shutdown no native cleanup.
- capability views check phase READY before native; capability never closes.

Access migration:
- delete core/AccessSession.kt; move SessionPhase/CleanupDisposition/AccessSessionFactory to core/access/AccessSession.kt + AccessSessionFactory.kt;
- factory contracts:
  ~~~
  interface AccessSessionFactory { val backendId: String; fun open(req: AccessSessionRequest): AccessResult<AccessSession> }
  data class AccessSessionRequest(backendId: String, lkmPath: String? = null, lkmFd: Int = -1, jniSession: JniAccessSession? = null, init: PageCacheInit43284? = null)
  class AccessSessionRegistry(map: Map<String, AccessSessionFactory>) { fun open(req): AccessResult<AccessSession> }
  ~~~
  registry validates key==backendId and duplicates, unknown backend/request field refuses. LkmSessionFactory backendId="lkm", opens LkmSession(lkmPath/lkmFd). Cve43284SessionFactory backendId="cve_2026_43284", requires jniSession+init; target derives only from init.carrierPath; pageTarget field removed. No 43499 key; 43499 open refuses no-access-session.
- implementations: LkmSession (kernelMemory), Cve43284Session (pageCache); 43499 has no access session;
- tests/fakes migrate: LkmKernelAccess tests, JniCapabilitySession tests, fake session; add close/shutdown/partial-null tests.

## 4. JNI boundaries

Capabilities are backed by backend-specific JNI:

~~~
43499: Cve43499Run/PipelineEngine -> jni.cve43499 native ports (no access session, no capability)
43284: Cve43284Session -> PageCacheKernelAccess -> JniPageCacheOps -> JniAccessSession -> jni.cve43284 session ops. NativeHelper/NativeRawEsp are used by pagecache stages, not by the capability path.
~~~

Backend-specific operations stay outside capabilities:
- 43499: discover, prepare, route/landing, actor batch, victim uid.
- 43284: stage pipeline, ELF/LKM patch, module precheck/vermagic, trigger/wait.

No common generic read/write JNI. The unified abstraction is Kotlin session/capability, not a shared native ABI.

## 5. PipelineEngine

~~~
data class RunPolicy(attempts: Int = 1, settleMs: Long = 0, skipPolicy: SkipPolicy = None)
enum class SkipPolicy { None, FrozenRemaining }

sealed interface RunNodeResult { Done(evidence); Retryable(reason); Refused(reason,evidence); Frozen(reason,evidence) }

interface RunNode {
  val backendId; val key; val token; val policy
  suspend fun run(): RunNodeResult
  fun calls(): List<String> = emptyList()
  fun abort(reason: String)
  fun close()
}

data class NodeDescriptor(backendId,key,token)
data class NodeOutcome(node: NodeDescriptor, result: RunNodeResult, calls: List<String>)
enum class Overall { Ok, Failed }
data class PipelineResult(outcomes, primary, cleanup, overall)
~~~

Semantics:
- order as list; per-node retry; exhausted Retryable -> Refused with all-attempt calls.
- Refused stops and emits no remaining nodes.
- Frozen stops; only Frozen consults skipPolicy: FrozenRemaining appends Frozen("engine-frozen") to remaining, None emits none.
- cleanup executes once in NonCancellable finally, never retries, is not aborted; cleanup not Done -> overall Failed, primary Frozen(cleanup).
- non-cancellation throwable from run -> synthetic Frozen with calls snapshot; abort then cleanup.
- CancellationException propagates with identity after abort/cleanup/reverse close.
- close: PipelineEngine owns nodes, reverse close once, idempotent; run-after-close rejected.
- overall Ok iff all main nodes and cleanup Done; primary first non-Done main, cleanup overrides.

Node adapters:
- Cve43499OpNode: Missing -> Refused(precondition:<token>-<input>) no call; Ready(empty) -> Refused(precondition:<token>-no-calls); Ready calls map ok/not-ready/indeterminate/failed.
- Cve43284StageNode: Stage.run mapping; returned CANCELLED -> Frozen; thrown CancellationException propagates; abort/close delegate to Stage.

## 6. BackendRun / ExecutionEngine

~~~
interface BackendRun : AutoCloseable {
  val backendId; suspend fun run(request, progress): BackendRunResult
  fun stop(); override fun close(); suspend fun shutdown()
}
sealed BackendRunResult { Ok(note,uid,steps: List<NodeOutcome>); Failed(reason,steps) }

interface ExecutionEngine : AutoCloseable { val state; suspend fun run(plan: Plan) }
class DelegatingExecutionEngine(run: BackendRun) : ExecutionEngine, AutoCloseable
interface BackendRunFactory { val backendId: String; fun create(): BackendRun }
class Cve43499RunFactory(route, probe) : BackendRunFactory { backendId="cve_2026_43499"; create()=Cve43499Run(route,probe) }
class Cve43284RunFactory(context, config) : BackendRunFactory { backendId="cve_2026_43284"; create()=Cve43284Run(context,config) }
class ExecutionEngineFactory(map<backendId, BackendRunFactory>) // validates key==backendId, duplicate rejected
~~~

All BackendRun/ExecutionEngine are one-shot; factory creates a fresh engine per execution; close exactly once; run-after-close/second-run rejected; cancellation calls non-suspend stop then rethrows.

## 7. Backend structures

43499:
~~~
backend.cve43499.ktstep.run    Cve43499RunEngine, Cve43499StepPlan, Cve43499StepOps, Cve43499RunFactory(route,probe)
backend.cve43499.ktstep.native NativeAttempt43499Port/Prepare/Syscalls
backend.cve43499.ktstep.discovery Cve43499DiscoverProcessor/Facts/Adapter
backend.cve43499.ktstep.route  LandingRoute/MulticastLanding43499Port/stubs
backend.cve43499.ktstep.victim
jni.cve43499                 JNI facade
glcommon discovery          libglcommon + jni.glcommon
~~~

43284:
~~~
backend.cve43284.access       Cve43284Session(AccessSession), Cve43284SessionFactory, PageCacheKernelAccess(PageCacheAccess view only)
backend.cve43284.native       JniAccessSession, JniPageCacheOps, PageCacheInit43284; JniPageCacheOps(session,target,role=PageCacheRole.Carrier): up/targets/readBlock/writeBlock/tearDown; handle owner JniAccessSession
# JniCapabilitySession deleted; its tests migrate to Cve43284Session
backend.cve43284.pagecache    PageCacheEngine(no runner), StageFactory->nodes, elf/lkm/stage/write, Cve43284RunFactory(context,config)
backend.lkm.access           LkmSession(AccessSession; KernelMemoryAccess), constructor lkmPath/fd, backendId="lkm"; LkmSessionFactory opens it; close owns fd once
jni.cve43284                  unchanged ABI
ghostlock43284               external artifact, not owned by 43499
~~~

## 8. UI boundary

UI imports only core.engine (ExecutionEngine/ExecutionState/ExecutionEngineFactory), backend.common, core.model, and existing cve43284.pagecache exceptions. No native/page/route internals.

## 9. Migration

1. Create final `core.access` contracts/factory and `backend.cve43284.access`, `backend.cve43284.native`, `backend.lkm.access` sessions/ops directly in final packages; add host/JNI tests. No temporary then-move for access classes.
2. PipelineEngine + RunNode + fakes; no production switch.
3. 43499 OpNode parity; delete StepEngine.
4. 43284 StageNode parity; delete StageRunner/StagePageCacheEngine.
5. BackendRun uses PipelineEngine; ExecutionEngineFactory.
6. Package moves; 43284 JNI/artifact unchanged.
7. Remove legacy KernelAccess from production.

## 10. Gates

- Access: factory interfaces compile as ordinary interfaces; registry duplicate/mismatch/unknown; Cve43284SessionFactory target only from init.carrierPath (no pageTarget); no JniCapabilitySession reference; PageCacheKernelAccess initialize/snapshot/targets/read/write/writeImage; shutdown order page->tearDown->jni.close once; close-in-flight/double-shutdown; LkmSession init/snapshot; 43499 no-access-session. Host + AVD gates.
- 43499 parity: order/retry/Refused/Frozen/skip/precondition/empty-ready.
- 43284 parity: stage order/partial/freeze/cleanup/restore/cancel.
- Unified: cancellation identity, cleanup once, close reverse once, one-shot, factory mismatch.
- Native/APK gates unchanged for glcommon/libgl43499/ghostlock43284.

## 11. Non-goals

- No common generic read/write JNI.
- No Step/Stage inheritance hierarchy.
- No 43284 JNI package/ABI move.
- UI/config wiring remains out of scope.
