package com.ghostlock.app.backend.declarations

import com.ghostlock.app.domain.run.RunInputArtifactKind

/**
 * Immutable declaration of one backend-private packaged carrier.
 *
 * @property logicalId Stable request-global artifact identity.
 * @property packagedFilename Exact filename below the application's native-library directory.
 * @property kind Fixed staged artifact kind; product carriers must be executable.
 * @note The declaration contains no Context, resolved path, descriptor, or mutable runtime state.
 */
internal data class BackendCarrierArtifact(
    val logicalId: String,
    val packagedFilename: String,
    val kind: RunInputArtifactKind = RunInputArtifactKind.EXECUTABLE,
) {
    init {
        require(logicalId.matches(Regex("^[a-z][a-z0-9._-]{0,63}$"))) { "invalid carrier logical id" }
        require(packagedFilename.matches(Regex("^lib[A-Za-z0-9._-]+\\.so$"))) {
            "invalid packaged carrier filename"
        }
        require(kind == RunInputArtifactKind.EXECUTABLE) { "backend carrier must be executable" }
    }
}
