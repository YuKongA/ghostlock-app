package com.ghostlock.app.data.runhost.client

import com.ghostlock.app.core.runhost.RunHostCode
import com.ghostlock.app.core.runhost.RunHostSnapshotModel

/** Shared typed client outcome for App and Shizuku transports. */
sealed interface RunHostClientOutcome {
    /** Host published a decoded immutable terminal snapshot. */
    data class Terminal(val snapshot: RunHostSnapshotModel) : RunHostClientOutcome

    /** A synchronous lifecycle method rejected the run. */
    data class Rejected(val code: RunHostCode) : RunHostClientOutcome

    /** Binder died or the typed wire violated its invariant. */
    data class TransportFailure(val code: RunHostCode) : RunHostClientOutcome
}
