package com.ghostlock.app.data.rootscript

import com.ghostlock.app.core.flow.RootScriptDecodeOutcome
import com.ghostlock.app.core.flow.RootScriptResultReader
import com.ghostlock.app.core.rootscript.RootScriptArtifact
import com.ghostlock.app.core.rootscript.RootScriptExecutionPlan
import com.ghostlock.app.core.rootscript.RootScriptResultCodec
import java.io.File

/** Android one-descriptor reader for one pre-bound root-script result object and plan. */
class AndroidRootScriptResultReader(
    artifact: RootScriptArtifact,
    private val plan: RootScriptExecutionPlan,
) : RootScriptResultReader {
    private val resultFile = File(artifact.resultPath)
    private val expectedIdentity = artifact.resultIdentity

    /**
     * Reads and decodes strict GLR1 without allowing expected physical or protocol failures to escape.
     *
     * @return Existing Flow decode outcome for the bound run.
     */
    override fun read(): RootScriptDecodeOutcome = when (
        val physical = PublishedArtifactAccess.readSameObject(
            file = resultFile,
            identity = expectedIdentity,
            maximumByteCount = RootScriptResultCodec.MAX_RESULT_BYTES,
        )
    ) {
        SameObjectRead.IdentityMismatch -> RootScriptDecodeOutcome.IdentityMismatch
        SameObjectRead.ReadFailure -> RootScriptDecodeOutcome.Malformed
        is SameObjectRead.Complete -> try {
            RootScriptDecodeOutcome.Complete(RootScriptResultCodec.decode(physical.bytes, plan))
        } catch (_: IllegalArgumentException) {
            RootScriptDecodeOutcome.Malformed
        }
    }
}
