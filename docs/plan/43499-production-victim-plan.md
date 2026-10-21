# 43499 production victim flow

Status: design-critic approved revision 6 (no blocking issues) and implemented (host + AVD victim lifecycle). No UI/config wiring; no uid=0 claim.

> **2026-10-21 指针**：批准状态见 [43499-approval-status.md](43499-approval-status.md)（唯一权威，本件不再自行声明）；
> 执行结构（PipelineEngine 唯一权威 / Stage 与 KtStep 只是 node 形态 / 会话 close 与执行 close 互斥）见
> [execution-architecture.md](execution-architecture.md)。
> 本件**已实现**（host + AVD lifecycle）；**无 uid=0 主张**。

## 1. Goal

Own a Kotlin victim session, run 43499, probe uid, and tear down on every path without leaking a child.

## 2. Why no native child JNI

The victim is a userspace process (a kernel task). C++ actor batch only performs page/actor primitives. The unused NativeAttemptJni child entries stay ENOSYS. This does not prove the kernel write changed the victim cred; that remains a device gate.

## 3. API and placement

Final package: `com.ghostlock.app.backend.cve43499.ktstep.run`.

Explicit pre-package exception for this batch: `KtStepPlan`, `KtStepOutcome`, `NativePrepareResult`, `KernelFutexWords`, `NativeAttemptPort`, `VictimProbe`, `VictimSession`, `VictimStartResult`, `VictimProcess`, `ProcessVictim`, `ProcessVictimSession` keep their existing names and `engine.ktstep` package. Package moves are a separate batch.

~~~
class Cve43499ProductionFlow(
    private val prepared: NativePrepareResult.Ok,
    private val words: KernelFutexWords,
    private val attempt: NativeAttemptPort,
    private val victimFactory: () -> VictimSession = { ProcessVictimSession() }, // default: /system/bin/sh, 2000 ms
    private val log: (String) -> Unit = {},
    private val runEngine: suspend (KtStepPlan, VictimProbe) -> KtStepOutcome = { plan, probe ->
        Cve43499ProductionEngine(prepared, words, attempt, probe, log).run(plan)
    },
) {
    suspend fun run(plan: KtStepPlan): KtStepOutcome
}
~~~

`victimFactory` and `runEngine` are the host seams. Caller owns prepared/words/attempt.

## 4. Victim result and lifecycle

~~~
sealed interface VictimStartResult {
    data object Ok : VictimStartResult
    data class Failed(val reason: String, val cause: Throwable? = null) : VictimStartResult
}

interface VictimSession : ProductionProbe, AutoCloseable {
    fun start(): SyscallResult
    fun startDetailed(): VictimStartResult = try {
        when (val answer = start()) {
            is SyscallResult.Ok -> VictimStartResult.Ok
            is SyscallResult.Errno -> VictimStartResult.Failed("victim-start:" + answer.code)
        }
    } catch (problem: Throwable) {
        VictimStartResult.Failed("victim-start:" + (problem.message ?: problem.javaClass.simpleName), problem)
    }
    fun close()
}
~~~

`ProcessVictimSession` constructor: `(command: List<String> = listOf("/system/bin/sh", "-c", "while read _; do id -u; done"), processFactory: (List<String>) -> VictimProcess = { ProcessVictim(it) }, probeTimeoutMs: Long = 2000)`. The flow default uses `ProcessVictimSession()`.

`ProcessVictimSession.startDetailed()` overrides the default: calls start(); Errno maps to `Failed("victim-start:"+code)`; any Throwable maps to `Failed("victim-start:"+type-or-message, cause)`. It never leaves a process.

`ProcessVictim` construction is atomic: process -> streams; stream failure performs bounded close and rethrows. Constructor seam `streamFactory` lets a host test throw after process creation.

`VictimProcess` interface: `uid(): Int?`, `close()`. Production `ProcessVictim` implements it. Ownership: `VictimProcess.close()` owns the bounded stream/destroy/destroyForcibly/wait sequence and is idempotent; `ProcessVictimSession.close()` only delegates to `victim.close()` then marks closed/shuts down the executor. It does not duplicate stream operations.

`uid()` timeout:
- per-session daemon single-thread executor;
- one future per probe, `get(probeTimeoutMs)`;
- timeout/exception -> close process, cancel future, `shutdownNow`, await 200 ms;
- return `Errno(ETIMEDOUT)` and mark failed; later probes -> ENOSYS;
- executor shutdown on close; no static pool.

Session `close()` bounded/idempotent: mark closed; `runCatching { victim?.close() }` (VictimProcess owns streams/destroy/wait); executor.shutdownNow; restore interrupt and continue. ProcessVictim.close implements the bounded sequence and swallows already-exited/destroy/stream exceptions.

## 5. Flow sequence

~~~
val victim = try {
    victimFactory()
} catch (problem: Throwable) {
    return KtStepOutcome(KtEnginePhase.Failed, "victim-start:create", emptyList(), emptyList(), uid = null)
}
try {
    when (val started = victim.startDetailed()) {
        VictimStartResult.Ok -> Unit
        is VictimStartResult.Failed ->
            return KtStepOutcome(KtEnginePhase.Failed, started.reason, emptyList(), emptyList(), uid = null)
    }
    return runEngine(plan, victim)
} finally {
    victim.close()
}
~~~

- start failure: finally closes; no child remains.
- run throws: finally closes; exception identity preserved.
- cancellation: finally closes; CancellationException identity preserved.
- flow never closes attempt.

## 6. Tests

Host flow-level:
- fake victim + runEngine success -> close once, outcome unchanged;
- fake victim start Errno -> Failed victim-start:<code>, empty steps/traces, uid null, close once;
- fake victim start throws -> Failed victim-start:<type>, close once;
- victimFactory throws -> Failed victim-start:create, no session;
- runEngine throws -> close once, identity;
- cancellation -> close once, identity.

Host session-level:
- ProcessVictim stream factory throws after process creation -> process destroyed once;
- ProcessVictim ignores destroy -> destroyForcibly; bounded close;
- fake VictimProcess close counter called exactly once by ProcessVictimSession.close, no direct stream ops;
- uid timeout -> Errno(ETIMEDOUT), close no hang, no live executor;
- close while probing -> no deadlock; double close no-op;
- already-exited/interrupted close -> no throw, interrupt restored.

AVD:
- real ProcessVictimSession start -> uid >= 0 -> close;
- production flow with fake runEngine -> lifecycle only; no uid=0 claim.

## 7. Non-goals

- No NativeAttemptJni child implementation.
- No uid=0/page-landing claim.
- No UI/config wiring.
- No C++ change.
