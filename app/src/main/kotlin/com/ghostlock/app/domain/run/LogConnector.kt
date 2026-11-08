package com.ghostlock.app.domain.run

/** Terminal state of one log source after every accepted draft has been emitted. */
sealed interface LogConnectorEnd {
    /** The producer ended normally and the connector drained its complete source FIFO. */
    data object Drained : LogConnectorEnd

    /**
     * The connector drained its accepted prefix but could not prove complete source delivery.
     *
     * @property reason Bounded source-local diagnostic; it never changes typed run facts.
     */
    data class Incomplete(val reason: String) : LogConnectorEnd {
        init {
            require(reason.isNotBlank()) { "incomplete log connector reason is blank" }
        }
    }
}

/**
 * Pull boundary between one source-local FIFO and the application log hub.
 *
 * The producer owner establishes EOF or a terminal marker before normal collection returns. [close] only releases
 * reader or transport resources after collection, except when aborting an active run.
 */
interface LogConnector : AutoCloseable {
    /**
     * Drains accepted drafts in source order.
     *
     * @param emit Suspended raw-ingress acceptance owned by the sole LogHub.
     * @return Whether the complete source or only its accepted prefix was drained.
     */
    suspend fun collect(emit: suspend (LogDraft) -> Unit): LogConnectorEnd
}
