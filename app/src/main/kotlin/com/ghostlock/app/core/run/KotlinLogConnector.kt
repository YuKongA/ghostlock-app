package com.ghostlock.app.core.run

import com.ghostlock.app.domain.run.LogConnector
import com.ghostlock.app.domain.run.LogConnectorEnd
import com.ghostlock.app.domain.run.LogDraft
import com.ghostlock.app.domain.run.LogEmitter
import java.util.concurrent.atomic.AtomicBoolean
import kotlinx.coroutines.channels.Channel

/** Unlimited channel-backed source used by Kotlin and coroutine producers for one run. */
internal class KotlinLogConnector : LogConnector, LogEmitter {
    private val input = Channel<LogDraft>(Channel.UNLIMITED)
    private val collecting = AtomicBoolean(false)
    private val terminalLock = Any()
    private var terminal: LogConnectorEnd? = null

    /** Enqueues one immutable source draft without blocking the producer. */
    override fun emit(record: LogDraft) {
        input.trySend(record)
    }

    /** Closes the write side after every producer has stopped and joined. */
    fun finishInput() {
        finish(LogConnectorEnd.Drained)
    }

    /**
     * Closes the write side after a producer failure while preserving every already accepted draft.
     *
     * @param reason Source-local failure diagnostic.
     */
    fun finishInputIncomplete(reason: String) {
        finish(LogConnectorEnd.Incomplete(reason))
    }

    /** Drains the source FIFO exactly once and returns its producer-owned terminal state. */
    override suspend fun collect(emit: suspend (LogDraft) -> Unit): LogConnectorEnd {
        check(collecting.compareAndSet(false, true)) { "Kotlin log connector is already collecting" }
        for (draft in input) emit(draft)
        return synchronized(terminalLock) {
            terminal ?: LogConnectorEnd.Incomplete("kotlin-source-closed-without-terminal")
        }
    }

    /** Aborts an active collection and releases the channel when the run itself is cancelled. */
    override fun close() {
        finish(LogConnectorEnd.Incomplete("kotlin-source-aborted"))
    }

    /** Commits the first source terminal state and closes the channel write side once. */
    private fun finish(end: LogConnectorEnd) {
        val shouldClose = synchronized(terminalLock) {
            if (terminal != null) {
                false
            } else {
                terminal = end
                true
            }
        }
        if (shouldClose) input.close()
    }
}
