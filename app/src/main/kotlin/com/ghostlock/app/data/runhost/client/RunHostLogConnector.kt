package com.ghostlock.app.data.runhost.client

import com.ghostlock.app.core.runhost.RunHostIdentity
import com.ghostlock.app.core.runhost.RunHostSnapshotModel
import com.ghostlock.app.data.runhost.wire.parcelDataSize
import com.ghostlock.app.domain.run.LogConnector
import com.ghostlock.app.domain.run.LogConnectorEnd
import com.ghostlock.app.domain.run.LogDraft
import com.ghostlock.app.run.RunHostLogRecord
import java.util.concurrent.atomic.AtomicBoolean
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.channels.Channel

/**
 * Application-side connector for one App or Shizuku RunHost callback stream.
 *
 * The typed terminal snapshot remains product authority. This connector independently proves delivery of the
 * source-private sequence through the terminal marker and reports gaps or peer death only as log incompleteness.
 */
internal class RunHostLogConnector(
    private val identity: RunHostIdentity,
    parcelSize: (RunHostLogRecord) -> Int = { record -> record.parcelDataSize() },
) : LogConnector {
    private val cursor = RunHostLogCursor(identity, parcelSize)
    private val events = Channel<Event>(Channel.UNLIMITED)
    private val collecting = AtomicBoolean(false)
    private val terminalLock = Any()
    private var terminalQueued = false
    private val marker = CompletableDeferred<Unit>()
    private val ended = CompletableDeferred<LogConnectorEnd>()

    /** Completes when a typed terminal-delivery marker has entered the source FIFO. */
    val terminalMarker: Deferred<Unit> = marker

    /** Completes after collection drains the complete source or its accepted prefix. */
    val end: Deferred<LogConnectorEnd> = ended

    /** Enqueues one oneway callback record without performing global ordering or sink work. */
    fun receive(record: RunHostLogRecord) {
        synchronized(terminalLock) {
            if (!terminalQueued) events.trySend(Event.Record(record))
        }
    }

    /** Enqueues the final source sequence/dropped count carried by the typed terminal snapshot. */
    fun receiveTerminal(snapshot: RunHostSnapshotModel) {
        val accepted = synchronized(terminalLock) {
            if (terminalQueued) {
                false
            } else {
                terminalQueued = true
                events.trySend(
                    if (snapshot.identity == identity) {
                        Event.Terminal(snapshot.logSequence, snapshot.droppedLogs)
                    } else {
                        Event.Incomplete("runhost-log-terminal-identity")
                    },
                )
                events.close()
                true
            }
        }
        if (accepted) marker.complete(Unit)
    }

    /** Ends a source without a terminal marker while preserving every record already in its FIFO. */
    fun finishIncomplete(reason: String) {
        synchronized(terminalLock) {
            if (terminalQueued) return
            terminalQueued = true
            events.trySend(Event.Incomplete(reason))
            events.close()
        }
    }

    /** Drains and validates one source FIFO exactly once. */
    override suspend fun collect(emit: suspend (LogDraft) -> Unit): LogConnectorEnd {
        check(collecting.compareAndSet(false, true)) { "RunHost log connector is already collecting" }
        var result: LogConnectorEnd? = null
        try {
            var failure: String? = null
            var terminalSeen = false
            for (event in events) {
                when (event) {
                    is Event.Record -> if (failure == null) {
                        when (val accepted = cursor.accept(event.record)) {
                            is RunHostLogCursorResult.Accepted -> emit(accepted.draft)
                            is RunHostLogCursorResult.Rejected -> failure = accepted.reason
                        }
                    }

                    is Event.Terminal -> {
                        terminalSeen = true
                        failure = failure ?: cursor.terminalFailure(event.finalSequence, event.droppedLogs)
                    }

                    is Event.Incomplete -> failure = failure ?: event.reason
                }
            }
            result = when {
                failure != null -> LogConnectorEnd.Incomplete(checkNotNull(failure))
                !terminalSeen -> LogConnectorEnd.Incomplete("runhost-log-terminal-marker-missing")
                else -> LogConnectorEnd.Drained
            }
            return result
        } catch (cancelled: CancellationException) {
            result = LogConnectorEnd.Incomplete("runhost-log-collection-cancelled")
            throw cancelled
        } catch (problem: Throwable) {
            result = LogConnectorEnd.Incomplete(
                "runhost-log-collection-" + problem::class.java.simpleName,
            )
            return result
        } finally {
            ended.complete(result ?: LogConnectorEnd.Incomplete("runhost-log-collection-stopped"))
        }
    }

    /** Aborts active transport collection; normal completion closes only after the marker has drained. */
    override fun close() {
        finishIncomplete("runhost-log-connector-aborted")
    }

    /** Source-private transport events; none is a LogHub commit or product result. */
    private sealed interface Event {
        data class Record(val record: RunHostLogRecord) : Event
        data class Terminal(val finalSequence: Long, val droppedLogs: Long) : Event
        data class Incomplete(val reason: String) : Event
    }
}
