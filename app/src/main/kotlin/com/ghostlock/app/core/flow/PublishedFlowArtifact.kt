package com.ghostlock.app.core.flow

import com.ghostlock.app.core.rootscript.PublishedArtifactIdentity
import com.ghostlock.app.domain.run.RunInputArtifactKind

/**
 * Host-local staged artifact borrowed by one backend Flow.
 *
 * @property logicalId Frozen request identity.
 * @property kind Validated artifact kind.
 * @property path Host-local ordinary-file path; callers must not reinterpret caller paths.
 * @property identity Identity captured from the staged closed object.
 * @note The RunHost owns the path and its lifetime; a Flow only borrows it for the accepted run.
 */
data class PublishedFlowArtifact(
    val logicalId: String,
    val kind: RunInputArtifactKind,
    val path: String,
    val identity: PublishedArtifactIdentity,
)
