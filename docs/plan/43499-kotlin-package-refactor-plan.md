# Kotlin package refactor plan — backend separation

Status: design-critic approved final revision with no blocking issues (2026-10-20). No code moved in this batch.

> **2026-10-21**：批准状态见 [43499-approval-status.md](43499-approval-status.md)（唯一权威）；执行结构见 [execution-architecture.md](execution-architecture.md)。
> 本件（包结构重构）**设计已批准**，包移动分批。

## 1. Problem

The Kotlin tree mixes generic execution, 43499 kt-step, 43499 legacy, 43284 page-cache, and UI/backends in flat packages:

- engine/ktstep contains 43499 engine, generic step adapter, native ports, page layout, route, victim, diagnostics;
- engine/kernelmemory contains 43499 legacy access;
- engine/pagecache contains 43284 page-cache;
- jni/cve43499 and jni/cve43284 already distinguish backends, but engine does not;
- names KtStepEngine, KtOpsInvoker, KernelRoute, KernelPageWriter do not say which backend or which layer they belong to.

## 2. Target package tree

    com.ghostlock.app.core.steps              # generic StepEngine/StepId/OpsInvoker
    com.ghostlock.app.core.engine             # generic ExecutionEngine

    com.ghostlock.app.jni.glcommon            # final owner of common discovery JNI
      NativeDiscoverJni (create/discover/close)
      DiscoveryHandle (opaque Long, owned by glcommon port)

    com.ghostlock.app.glcommon.discovery      # generic libglcommon discovery
      DiscoveryParams/Result/Codec
      NativeDiscoverPort (imports jni.glcommon only)
      # no 43499 page/alias/route types, no NativeDiscover43499Port

    com.ghostlock.app.jni.cve43499             # sole owner of 43499 external JNI objects
      NativeAttemptJni
      NativePrepareJni
      NativeDiscoverJni (until phase 6)
      NativeSyscalls43499Jni
      NativeMulticastLanding43499Jni
      NativeSelectStackLanding43499Jni (stub)
      NativeTcpZerocopyLanding43499Jni (stub)

    com.ghostlock.app.backend.cve43499
      ktstep.run
        Cve43499RunEngine
        Cve43499StepPlan
        Cve43499StepOps
        Cve43499RunOutcome
      ktstep.layout
        Cve43499Layout
        Cve43499PageWriter
        Cve43499FutexWords
        PayloadSlots
      ktstep.native
        NativeAttempt43499Port/Params/Result
        NativePrepare43499Port/Params/Result
        Cve43499Syscalls
      ktstep.discovery
        Cve43499DiscoverAdapter      # maps generic discovery -> Cve43499DiscoverFacts
        Cve43499DiscoverFacts        # found, mmStruct, wordsBase, wordsBytes
        Cve43499DiscoverProcessor    # alias/page from facts + payloadOffset + userPage
        Cve43499PageAddresses        # aliasDelta/pageBase/fakeTask/fakeLock/waiterWord
        Cve43499DiscoverDecision     # Ready(addresses) / Refused(reason)
      ktstep.route
        LandingRoute
        MulticastLandingRoute
        MulticastLanding43499Port/Params/Result
        SelectStackLandingRoute (stub)
        TcpZerocopyLandingRoute (stub)
      ktstep.victim
        ProcessVictim/ProcessVictimSession/VictimProbe
      ktstep.diagnostics
        KotlinPiActors/KotlinPiRace/PiActorsKernelRoute/PiProbeRunner
      # no legacy package in production; kernelmemory/Native43499/Assembled move to legacy test source set only

    com.ghostlock.app.backend.cve43284
      pagecache
        PageCacheEngine
        stage/*
        write/*

    com.ghostlock.app.backend.common
      EngineLog

## 3. Rename table

| current | target | reason |
|---|---|---|
| engine.ktstep.KtStepEngine | backend.cve43499.ktstep.run.Cve43499RunEngine | backend-qualified; distinguishes from generic StepEngine |
| engine.ktstep.KtOpsInvoker | backend.cve43499.ktstep.run.Cve43499StepOps | it is the 43499 OpsInvoker, not generic |
| engine.ktstep.KtStepPlan | backend.cve43499.ktstep.run.Cve43499StepPlan | backend-qualified |
| engine.ktstep.KtStepOutcome | backend.cve43499.ktstep.run.Cve43499RunOutcome | same |
| engine.ktstep.KernelLayout | backend.cve43499.ktstep.layout.Cve43499Layout | not a kernel-wide layout |
| engine.ktstep.KernelPageWriter | backend.cve43499.ktstep.layout.Cve43499PageWriter | same |
| engine.ktstep.KernelRoute | backend.cve43499.ktstep.route.LandingRoute | route landing, not generic kernel |
| engine.ktstep.DiscoverProcessor | backend.cve43499.ktstep.discovery.Cve43499DiscoverProcessor | backend-qualified |
| engine.ktstep.K3PageFixture | backend.cve43499.ktstep.diagnostics.Cve43499PageFixture | diagnostic fixture |
| engine.ktstep.PageWriter.kt | backend.cve43499.ktstep.layout.Cve43499PageWriterContract | layer-qualified |
| engine.ktstep.KernelSyscalls | backend.cve43499.ktstep.native.Cve43499Syscalls | backend-qualified |
| engine.ktstep.SyscallFutexWord | backend.cve43499.ktstep.layout.Cve43499SyscallFutexWord | backend-qualified |
| KernelPageAddresses | backend.cve43499.ktstep.discovery.Cve43499PageAddresses | discovery result |
| DiscoverProcessResult | backend.cve43499.ktstep.discovery.Cve43499DiscoverDecision | discovery result |
| PayloadSlots | backend.cve43499.ktstep.layout.PayloadSlots (unchanged) | layout input |
| engine.kernelmemory.* | backend.cve43499.legacy.* | legacy isolation |
| engine.pagecache.* | backend.cve43284.pagecache.* | backend ownership |
| engine.EngineLog | backend.common.EngineLog | shared engine logging |
| core.steps.StepEngine | unchanged | generic, backend-neutral |

Complete remaining inventory:

| file | target |
|---|---|
| ktstep/DiscoverProcessor.kt | backend.cve43499.ktstep.discovery |
| ktstep/K3PageFixture.kt | backend.cve43499.ktstep.diagnostics |
| ktstep/KernelFutexWords.kt | backend.cve43499.ktstep.layout.Cve43499FutexWords |
| ktstep/PageWriter.kt | backend.cve43499.ktstep.layout |
| ktstep/KernelSyscalls.kt | backend.cve43499.ktstep.native |
| ktstep/SyscallFutexWord.kt | backend.cve43499.ktstep.layout |
| ktstep/KtVictimFlow.kt | backend.cve43499.ktstep.victim.Cve43499VictimFlow |
| ktstep/ProcessVictim*.kt | backend.cve43499.ktstep.victim |
| ktstep/VictimProbe.kt | backend.cve43499.ktstep.victim |
| ktstep/KotlinPi*.kt | backend.cve43499.ktstep.diagnostics |
| ktstep/Pi*.kt | backend.cve43499.ktstep.diagnostics |
| ktstep/NativeAttempt*.kt | backend.cve43499.ktstep.native (JNI object to jni.cve43499) |
| ktstep/NativePrepare.kt | backend.cve43499.ktstep.native (JNI object to jni.cve43499) |
| ktstep/NativeDiscover.kt | glcommon.discovery + backend.cve43499.ktstep.discovery adapter |
| kernelmemory/Backend43499Port.kt | legacy test source set only |
| kernelmemory/Facts43499.kt | legacy test source set only |
| kernelmemory/KernelMemoryRun.kt | legacy test source set only |
| kernelmemory/PortOpsInvoker.kt | legacy test source set only |
| kernelmemory/JniBackend43499Port.kt | legacy test source set only |
| kernelmemory/KernelMemoryEngine.kt | legacy test source set only |
| jni/cve43499/Native43499.kt | legacy test source set only |
| jni/cve43499/Assembled43499.kt | legacy test source set only |

Native class names keep the backend suffix:

- NativeAttemptJni (jni.cve43499) + NativeAttempt43499Port/Params/Result (backend.native)
- NativePrepareJni (jni.cve43499) + NativePrepare43499Port/Params/Result (backend.native)
- NativeMulticastLanding43499Jni (jni.cve43499) + NativeMulticastLanding43499Port/Params/Result (backend.route)

## 3a. JNI ABI policy (must be explicit)

Existing JNI classes live in engine.ktstep and their C++ symbols are `Java_com_ghostlock_app_engine_ktstep_*`. Moving them silently changes the ABI. The refactor therefore has a dedicated JNI phase:

- stable facade package for 43499 JNI: `com.ghostlock.app.jni.cve43499`;
- classes moved there: NativeAttemptJni, NativePrepareJni, NativeDiscoverJni (until glcommon move), NativeSyscalls43499Jni, new NativeMulticastLanding43499Jni;
- C++ symbols updated in the same phase: old prefix `Java_com_ghostlock_app_engine_ktstep_` -> new `Java_com_ghostlock_app_jni_cve43499_`;
- temporary forwarding symbols for one release/batch, removed after AVD load tests and nm gates pass;
- `nm` required/forbidden lists and Android load tests updated in the same batch;
- no phase may claim package-only/no-behavior while changing JNI class packages.

Generic discovery JNI (`NativeDiscoverJni`) final owner is `com.ghostlock.app.jni.glcommon`; phase 6 moves it there with C++ symbol prefix `Java_com_ghostlock_app_jni_glcommon_NativeDiscoverJni_`, a forwarding window, nm update and Android load test. Until phase 6 it stays in `jni.cve43499`.

## 3b. Generic discovery contract

`glcommon.discovery` owns only kernel-generic fields:

- Params: wordsAddress, wordsBytes, collisions, threadCount, possibleCpus, pageSize, mmStructSize, mmSlabOrder, headerBytes, totalPokes, wordBytes, handleBytes, wakeRounds, roundTimeoutUs, rangeStart, rangeEnd, counterOffset, sweepTags. No proofCount/proofSeed: the current 120-byte wire has no slot for them.
- Result: ran, verdict, refusal, found, mmStruct, probes, rounds, pokes, error, wordsBase, wordsBytes.
- Codec: 120-byte params / 64-byte result, little endian, current magic/version. Adapter mapping is 1:1; no wire change.
- `Cve43499DiscoverFacts(found, mmStruct, wordsBase, wordsBytes)` is the adapter output; `Cve43499DiscoverProcessor.process(facts, userPageAddress)` uses `payloadOffset` injected at construction and the same slots as today, producing `Cve43499DiscoverDecision.Ready(Cve43499PageAddresses(...))` or `Refused(reason)`. Adapter mapping is generic result -> facts; processor is 43499-only. A host test must reproduce the current alias/page values from a synthetic facts record.

Discovery handle lifecycle: `jni.glcommon.NativeDiscoverJni.create()` returns a handle; `glcommon.discovery.NativeDiscoverPort` owns it and calls `close()`; `NativeDiscoverJni.discover(handle, params)` uses it. The 43499 adapter never creates/closes a handle and never imports jni.cve43499.

`Cve43499DiscoverAdapter` maps this generic port 1:1 to `Cve43499DiscoverFacts`; `Cve43499DiscoverProcessor` consumes facts + userPage. It must not change the wire. `NativeDiscover43499Port` is removed; the 43499 adapter is the only consumer.

## 3c. Exact JNI file split

| current file | JNI object -> jni.* | port/params/result -> backend/glcommon |
|---|---|---|
| NativeAttemptJni.kt | jni.cve43499.NativeAttemptJni | (NativeAttempt.kt) backend.cve43499.ktstep.native |
| NativePrepare.kt | jni.cve43499.NativePrepareJni | backend.cve43499.ktstep.native |
| NativeDiscover.kt | jni.glcommon.NativeDiscoverJni (phase 6; initially jni.cve43499) | glcommon.discovery generic; backend.cve43499.ktstep.discovery adapter |
| NativeSyscalls43499.kt | jni.cve43499.NativeSyscalls43499Jni | backend.cve43499.ktstep.native.Cve43499Syscalls + NativeKernelSyscalls |
| NativeMulticastLanding43499Jni (new) | jni.cve43499 | backend.cve43499.ktstep.route port/params/result |
| select/tcp stub JNI (new) | jni.cve43499 | backend.cve43499.ktstep.route stubs |

Class-level split:

| current class | target |
|---|---|
| NativeAttemptJni | jni.cve43499.NativeAttemptJni |
| NativeJniAttemptPort | backend.cve43499.ktstep.native.NativeAttempt43499Port |
| NativeAttemptParams/Result/Codec | backend.cve43499.ktstep.native |
| NativePrepareJni | jni.cve43499.NativePrepareJni |
| NativePrepare43499Port/Params/Result/Codec | backend.cve43499.ktstep.native |
| NativeDiscoverJni | jni.glcommon (phase 6; jni.cve43499 before) |
| NativeDiscoverParams/Result/Codec/Port | glcommon.discovery generic; Cve43499DiscoverAdapter backend.discovery |
| NativeSyscalls43499 | jni.cve43499.NativeSyscalls43499Jni |
| NativeKernelSyscalls/KernelSyscalls | backend.cve43499.ktstep.native |
| NativeMulticastLanding43499Jni | jni.cve43499 |
| MulticastLanding43499Port/Params/Result | backend.cve43499.ktstep.route |

Each split is its own phase; no file may contain both an external object and a backend port after phase 1.

## 3d. Discovery JNI session (separate native batch)

The generic discovery JNI needs a handle API that does not exist today. This is new native behavior and lands as a separate batch **before phase 6, while NativeDiscoverJni is still in jni.cve43499**:

- C++ `native_discover_session_43499.cpp` owns a small session object; `create()` returns a positive handle (0 on failure); `discover(handle, params)` runs; `close(handle)` destroys; invalid/0/closed handle returns a named error; close is idempotent.
- Interim symbols: `Java_com_ghostlock_app_jni_cve43499_NativeDiscoverJni_create/discover/close` (because the Kotlin class is still in jni.cve43499 until phase 6). Phase 6 moves them to `...jni_glcommon...` with a forwarding window.
- C++ TU is added to ktstep_android_core and exported by gl43499native for the interim; phase 6 moves it to glcommon_core/libglcommon.
- Android lifecycle tests: create -> discover -> close -> close, invalid handle, closed handle, malformed params; nm/load test for all three symbols.
- The Kotlin glcommon port never borrows `NativeAttemptJni`; the 43499 adapter never creates a handle.
- This batch has its own gate; the package refactor does not claim no-behavior for it.

## 3e. Final glcommon target contract

- `glcommon_core` STATIC sources: `steps/discovery_runner.cpp`, `platform/native_discover_session.cpp`, `platform/native_discover_common.cpp` (renamed from native_discover_43499.cpp). Links common_core/memory_core; snitch_setup.cpp is in common_core, not duplicated.
- `libglcommon.so` SHARED sources: `boundary/jni/native_discover_jni.cpp`; links glcommon_core, common_core, log.
- Interim before phase 6: `native_discover_session_43499.cpp` stays in `ktstep_android_core` and `native_discover_jni.cpp` stays in `gl43499native`; final phase 6 moves/renames to `native_discover_session.cpp` in `glcommon_core` and `libglcommon`. No source appears in both targets at one time; configure assertion covers this.
- APK: `libglcommon.so` is always packaged; `prepareGhostlockJniLibs` copies it; `verifyJniLibs` requires it and `nm -D libglcommon.so` requires `Java_com_ghostlock_app_jni_glcommon_NativeDiscoverJni_create/discover/close`, forbids 43499 route/access symbols.
- Kotlin: `jni.glcommon.NativeDiscoverJni.LIBRARY = "glcommon"`; `NativeDiscoverPort` loads it. Final test `jni.glcommon.NativeDiscoverJniGlcommonLifecycleTest` is Android instrumented, package `com.ghostlock.app.jni.glcommon`, runs after phase 6, loads `System.loadLibrary("glcommon")`.

## 4. Package dependency rules

- core.steps/core.engine must not import backend.* or glcommon.*.
- glcommon.discovery must not import backend.cve43499.* or page/layout/route.
- backend.cve43499.ktstep.native/route may import jni.cve43499 (JNI calls only).
- glcommon.discovery may import jni.glcommon (JNI calls only).
- jni.cve43499/jni.glcommon must not import backend/glcommon implementation packages; only ports may import JNI, never the reverse.
- backend.cve43499.legacy may not be imported by ktstep.run/layout/native/route/victim.
- backend.cve43284.pagecache must not import cve43499.ktstep.
- UI may import `core.engine` only for `ExecutionEngine`, `ExecutionState`, `ExecutionEngineFactory`, and their sealed state types; `backend.common`, `core.model`, and the current explicit exceptions `backend.cve43284.pagecache.PageCacheEngine` / `backend.cve43284.pagecache.LkmImagePatch` (AttackService/RunCoordinator/FinishExecution) until a 43284 API package exists. No UI import of cve43499 engine/native/page/route or legacy.

## 5. Migration phases

1. JNI facade ABI phase: move 43499 JNI objects to com.ghostlock.app.jni.cve43499, update C++ Java_ symbols, add temporary forwarding symbols, update nm/Android load tests. This is an ABI change, not no-behavior.
2. Mechanical backend package moves for non-JNI engine.ktstep files to backend.cve43499.ktstep.*; keep NativeDiscover in 43499 until phase 6.
3. Rename types in the table; update tests in the same batch.
4. Move 43499 legacy classes to backend.cve43499.legacy and exclude from production source set; move tests to legacy source set.
5. Move 43284 page-cache to backend.cve43284.pagecache.
6. Move EngineLog to backend.common; then split generic discovery into glcommon.discovery and leave Cve43499DiscoverAdapter in backend.cve43499.ktstep.discovery.
7. Add package-dependency + JNI-symbol/package tests and run full gates.

## 6. Tests

- Compile all production and test source sets after each phase.
- Update test package imports; keep test class names where possible.
- Add a source-scan test for every §4 rule, including test sources:
  - no backend.cve43499.legacy import from ktstep;
  - no cve43499 import from cve43284;
  - no backend/glcommon import from core.steps;
  - no backend.cve43499 import from glcommon.discovery;
  - UI imports only approved backend entry points.
- Legacy tests moved to app/src/legacy43499/test: KernelMemoryEngineTest, JniBackend43499PortTest, PortOpsInvokerTest, FakeBackend43499Port, TestDtos; old package names removed from production test set.
- 43499 tests move with their package: DiscoverProcessorTest -> cve43499.ktstep.discovery; KtStepEngineTest/NativeAttemptCodecTest -> run/native; KernelLayoutTest/KernelCredentialTest/FutexWordBridgeTest -> layout; KtVictimFlowTest/ProcessVictim* -> victim; KotlinPi*/Pi* -> diagnostics; ProductionBoundaryTest updated to new invariants; EngineLogTest -> backend.common; K3PageFixtureTest -> diagnostics.
- 43284 pagecache tests move to backend.cve43284.pagecache; expected count per phase recorded in the batch gate.
- Support/fakes: FakeKernelSyscalls -> backend.cve43499.ktstep.native test; FakeKernelRoute -> route test; FakeVictimProbe -> victim test; KernelSyscallsTest -> native test; NativeAttemptCodecTest -> native test; discovery codec/adapter tests -> glcommon/backend discovery tests; Cve43499DiscoverFactsTest + Cve43499DiscoverProcessorTest reproduce alias/page; Android NativeDiscoverJniLifecycleTest covers create/discover/close/idempotent/invalid/closed. ProductionBoundaryTest updated to final JNI packages and absence of Native43499/Assembled.
- Host facts/processor tests: app/src/test/kotlin/com/ghostlock/app/backend/cve43499/ktstep/discovery/Cve43499DiscoverFactsTest.kt and Cve43499DiscoverProcessorTest.kt, link ktstep_host_core; assertions equal current aliasDelta/pageBase/fakeTask/fakeLock/waiterWord for a synthetic facts record. Android JNI lifecycle has two tests: interim `jni.cve43499.NativeDiscoverJniCve43499LifecycleTest` (loads gl43499native, asserts Java_com_ghostlock_app_jni_cve43499_NativeDiscoverJni_create/discover/close), final `jni.glcommon.NativeDiscoverJniGlcommonLifecycleTest` (loads glcommon, asserts Java_com_ghostlock_app_jni_glcommon_NativeDiscoverJni_*); both cover create/discover/close/idempotent/invalid/closed.
- JNI symbol/package test: map each external class to required Java_ prefix (jni.cve43499, glcommon.discovery) and fail on mismatch; deliberate wrong-package fixture must fail.
- Encode every §4 edge: no ktstep.* -> legacy, no cve43284 -> cve43499, no glcommon -> cve43499, no core -> backend/glcommon, backend.ktstep.native/route -> jni.cve43499, glcommon.discovery -> jni.glcommon, jni.* -> core only, no jni.* -> backend/glcommon. Add one negative fixture per edge.
- Legacy JNI exclusion: production host/Android source sets exclude Native43499/Assembled43499 and libghostlock43499; the legacy43499/test source set may contain/reference them. Symbol checks forbid Java_com_ghostlock_app_jni_cve43499_Native43499_ in gl43499native/ghostlock_jni.
- Run full JVM/Android suites and record counts.

## 7. Risks / rollback

- Large mechanical import churn; one phase per commit/batch.
- Avoid changing behavior while moving; no route/native refactor in the same batch.
- Rollback is package/import revert per phase; no native changes.
- UI/config wiring remains out of scope.
