package com.ghostlock.app.engine.ktstep

import java.io.IOException
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.TimeoutException

/** Result of starting a victim session; the reason is user-visible evidence. */
sealed interface VictimStartResult {
    data object Ok : VictimStartResult
    data class Failed(val reason: String, val cause: Throwable? = null) : VictimStartResult
}

/** K4 victim lifecycle: start before probe; close must cover success/failure/cancel. */
interface VictimSession : ProductionProbe, AutoCloseable {
    fun start(): SyscallResult

    /** Default mapping; implementations may override to preserve exception types. */
    fun startDetailed(): VictimStartResult = try {
        when (val answer = start()) {
            is SyscallResult.Ok -> VictimStartResult.Ok
            is SyscallResult.Errno -> VictimStartResult.Failed("victim-start:" + answer.code)
        }
    } catch (problem: Throwable) {
        VictimStartResult.Failed("victim-start:" + (problem.message ?: problem.javaClass.simpleName), problem)
    }
}

/** Kotlin child victim; command is fixed, hosts may inject /bin/sh for tests. */
class ProcessVictimSession(
    private val command: List<String> = listOf("/system/bin/sh", "-c", "while read _; do id -u; done"),
    private val processFactory: (List<String>) -> VictimProcess = { ProcessVictim(it) },
    private val probeTimeoutMs: Long = 2000L,
) : VictimSession {
    @Volatile private var victim: VictimProcess? = null
    @Volatile private var closed = false
    private val probeGate = Any()
    private val executor = Executors.newSingleThreadExecutor { runnable ->
        Thread(runnable, "ghostlock-victim-probe").apply { isDaemon = true }
    }

    override val kind: ProbeKind = ProbeKind.Production

    override fun start(): SyscallResult = try {
        if (victim != null) SyscallResult.Ok(0L) else {
            victim = processFactory(command)
            SyscallResult.Ok(0L)
        }
    } catch (failed: IOException) {
        SyscallResult.Errno(KernelErrno.EPIPE)
    }

    override fun startDetailed(): VictimStartResult {
        if (closed) return VictimStartResult.Failed("victim-start:closed")
        return try {
            when (val answer = start()) {
                is SyscallResult.Ok -> VictimStartResult.Ok
                is SyscallResult.Errno -> VictimStartResult.Failed("victim-start:" + answer.code)
            }
        } catch (problem: Throwable) {
            victim?.let { runCatching { it.close() } }
            victim = null
            VictimStartResult.Failed("victim-start:" + (problem.message ?: problem.javaClass.simpleName), problem)
        }
    }

    override fun uid(): SyscallResult {
        if (closed) return SyscallResult.Errno(KernelErrno.ENOSYS)
        val current = victim ?: return SyscallResult.Errno(KernelErrno.ENOSYS)
        synchronized(probeGate) {
            if (closed) return SyscallResult.Errno(KernelErrno.ENOSYS)
            val future = executor.submit<Int?> { current.uid() }
            return try {
                val value = future.get(probeTimeoutMs, TimeUnit.MILLISECONDS)
                    ?: return SyscallResult.Errno(KernelErrno.EPIPE)
                SyscallResult.Ok(value.toLong())
            } catch (timeout: TimeoutException) {
                runCatching { current.close() }
                future.cancel(true)
                executor.shutdownNow()
                closed = true
                victim = null
                SyscallResult.Errno(KernelErrno.ETIMEDOUT)
            } catch (interrupted: InterruptedException) {
                Thread.currentThread().interrupt()
                SyscallResult.Errno(KernelErrno.EINTR)
            } catch (problem: Throwable) {
                SyscallResult.Errno(KernelErrno.EPIPE)
            }
        }
    }

    override fun close() {
        if (closed) return
        closed = true
        victim?.close()
        victim = null
        executor.shutdownNow()
    }
}
