package com.ghostlock.app.backend.transport

import com.ghostlock.app.run.IRunHost
import java.util.concurrent.atomic.AtomicBoolean
import kotlinx.coroutines.delay

/**
 * Polls the durable typed snapshot so terminal authority does not depend on callback delivery.
 *
 * @param host Current typed host binding.
 * @param identity Immutable run identity.
 * @param completed Shared client completion linearization flag.
 * @param finish At-most-once outcome publisher owned by the transport client.
 * @note Callback delivery may win this race; polling never replays diagnostic events.
 */
internal suspend fun pollRunHostTerminal(
    host: IRunHost,
    identity: RunHostIdentity,
    completed: AtomicBoolean,
    finish: (RunHostClientOutcome) -> Unit,
) {
    while (!completed.get()) {
        val wire = runCatching { host.getSnapshot(identity.runId, identity.generation) }
            .getOrElse {
                finish(RunHostClientOutcome.TransportFailure(RunHostCode.HOST_DIED))
                return
            }
        val reply = runCatching { RunHostSnapshotReplyModel.fromWire(wire) }
            .getOrElse {
                finish(RunHostClientOutcome.TransportFailure(RunHostCode.INTERNAL_CONTRACT_FAILURE))
                return
            }
        if (reply.code != RunHostCode.OK) {
            finish(RunHostClientOutcome.TransportFailure(reply.code))
            return
        }
        val snapshot = reply.snapshot
        if (snapshot == null || snapshot.identity != identity) {
            finish(RunHostClientOutcome.TransportFailure(RunHostCode.INTERNAL_CONTRACT_FAILURE))
            return
        }
        when (snapshot.state) {
            RunHostState.TERMINAL -> {
                finish(RunHostClientOutcome.Terminal(snapshot))
                return
            }

            RunHostState.DESTROYED -> {
                finish(RunHostClientOutcome.TransportFailure(RunHostCode.INTERNAL_CONTRACT_FAILURE))
                return
            }

            RunHostState.NEW, RunHostState.PREPARED, RunHostState.RUNNING ->
                delay(SNAPSHOT_POLL_INTERVAL_MS)
        }
    }
}

private const val SNAPSHOT_POLL_INTERVAL_MS: Long = 250L
