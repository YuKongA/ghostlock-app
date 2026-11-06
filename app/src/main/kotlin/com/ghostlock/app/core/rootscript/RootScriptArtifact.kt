package com.ghostlock.app.core.rootscript

/**
 * One published root-script contract consumed by a backend-specific rooted carrier.
 *
 * @property runId Stable run identity embedded in the script result.
 * @property generation Monotonic run generation.
 * @property scriptPath Published script path in the RunHost namespace.
 * @property resultPath Pre-created result-file path in the same workspace.
 * @property scriptIdentity Closed script object and byte identity.
 * @property resultIdentity Pre-execution result object identity; bytes are expected to change.
 * @property retainedArtifacts Host-local paths explicitly retained after terminal.
 */
data class RootScriptArtifact(
    val runId: String,
    val generation: Long,
    val scriptPath: String,
    val resultPath: String,
    val scriptIdentity: PublishedArtifactIdentity,
    val resultIdentity: PublishedArtifactIdentity,
    val retainedArtifacts: List<String>,
)
